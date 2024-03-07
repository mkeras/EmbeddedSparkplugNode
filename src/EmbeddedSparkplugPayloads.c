/*
Copyright 2024 Michael Keras.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "EmbeddedSparkplugPayloads.h"

static bool _NODE_INITIALIZED = false;
static BufferValue* _ENCODE_BUFFER = NULL;
static StreamFunction* _ENCODE_STREAM = NULL;

static const char* _bdseq_tag_name = "bdSeq";
static const int _bdseq_tag_alias = -1000;
static const char* _rebirth_tag_name = "Node Control/Rebirth";
static const int _rebirth_tag_alias = -1001;
static const char* _scan_rate_tag_name = "Node Control/Scan Rate";

static const size_t _INCOMING_STRING_MAX_LEN = 1024;
static const size_t _INCOMING_BUFFER_MAX_LEN = 1024;

static const uint32_t _SCAN_RATE_MIN = 500;
static const uint32_t _SCAN_RATE_MAX = 600000;

/*
Static declarations
*/

// This struct will be declared and used in a future version,
// for use in functions to add multiple properties to a Metric
 typedef struct {
     void **array_ptrs; // Array of void pointers
     size_t array_len; // Number of pointers
} VoidArray;


/*
Nanopb encode functions
*/


static bool _encode_to_stream_callback(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
    StreamFunction userFn = (StreamFunction)stream->state;
    if (userFn == NULL) return false;

    userFn((uint8_t*)buf, count);
    return true;
}


static bool _encode_payload(Payload* payload, BufferValue* buffer_ptr, StreamFunction* encodeFn) {
    // Encode to either user defined streaming function or to a buffer
    pb_ostream_t stream;
    if (buffer_ptr != NULL) {
        stream = pb_ostream_from_buffer(buffer_ptr->buffer, buffer_ptr->allocated_length);
        if (!pb_encode(&stream, Payload_fields, payload)) {
            // Encode failed
            buffer_ptr->written_length = 0;
            return false;
        }
        // Encode successful
        buffer_ptr->written_length = stream.bytes_written;
        return true;
    } else if (encodeFn != NULL) {
        stream.bytes_written = 0;
        stream.callback = _encode_to_stream_callback;
        stream.max_size = SIZE_MAX;
        stream.state = (void*)encodeFn;
        return pb_encode(&stream, Payload_fields, payload);
    } else {
        // No encoding target was supplied
        return false;
    }
}


static bool _pb_encode_string_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    const char* str = (const char *)(*arg);
    if (!pb_encode_tag_for_field(stream, field)) return false;
    if (!pb_encode_string(stream, (const uint8_t *)str, strlen(str))) return false;
    return true;
}


/*
The following 2 functions encode properties to metrics.
In a future version more value options and functions for handling array of properties will be added
*/
static bool _pb_encode_property_names(pb_ostream_t *stream, const pb_field_iter_t *field, void *const *arg) {
    char **property_key_array = *(char ***)arg;

    for (size_t i = 0; property_key_array[i] != NULL; i++) {
        if (!pb_encode_tag_for_field(stream, field)) return false;
        const char* str_arg = property_key_array[i];
        if (!pb_encode_string(stream, (const uint8_t*)str_arg, strlen(str_arg))) return false;
    }

    return true;
}


static bool _pb_encode_property_values(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    Payload_PropertyValue** property_values = *(Payload_PropertyValue ***)arg;
    for (size_t i = 0; property_values[i] != NULL; i++) {
        if (!pb_encode_tag_for_field(stream, field)) return false;
        
        if (!pb_encode_submessage(stream, Payload_PropertyValue_fields, property_values[i])) return false;
        
    }
    return true;
}


static bool _pb_encode_bytes_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    const BufferValue* buffer_value = (const BufferValue*)(*arg);
    if (!pb_encode_tag_for_field(stream, field)) return false;
    if (!pb_encode_string(stream, (const uint8_t *)(buffer_value->buffer), buffer_value->written_length)) return false;
    return true;
}


static bool _pb_encode_single_metric_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    Payload_Metric* metric = (Payload_Metric*)(*arg);
    if (!pb_encode_tag_for_field(stream, field)) return false;
    if (!pb_encode_submessage(stream, Payload_Metric_fields, metric)) return false;
    return true;
}


static void _basic_value_to_metric(BasicValue* value, Payload_Metric* metric) {
    metric->datatype = (uint32_t)(value->datatype);
    metric->has_datatype = true;
    metric->has_timestamp = true;
    metric->timestamp = value->timestamp;
    if (value->isNull) {
        metric->has_is_null = true;
        metric->is_null = true;
        return;
    }
    switch (value->datatype) {
        case spInt8:
        case spInt16:
        case spInt32:
        case spUInt8:
        case spUInt16:
        case spUInt32:
            metric->which_value = Payload_Metric_int_value_tag;
            metric->value.int_value = value->value.uint32Value;
            break;
        case spInt64:
        case spDateTime:  // Datetime is a uint64
        case spUInt64:
            metric->which_value = Payload_Metric_long_value_tag;
            metric->value.long_value = value->value.uint64Value;
            break;
        case spFloat:
            metric->which_value = Payload_Metric_float_value_tag;
            metric->value.float_value = value->value.floatValue;
            break;
        case spDouble:
            metric->which_value = Payload_Metric_double_value_tag;
            metric->value.double_value = value->value.doubleValue;
            break;
        case spBoolean:
            metric->which_value = Payload_Metric_boolean_value_tag;
            metric->value.boolean_value = value->value.boolValue;
            break;
        case spText:  // Text is a string
        case spUUID: // UUID is a string
        case spString:
            metric->which_value = Payload_Metric_string_value_tag;
            metric->value.string_value.funcs.encode = _pb_encode_string_callback;
            metric->value.string_value.arg = (void*)(value->value.stringValue);
            break;
        case spBytes:
            metric->which_value = Payload_Metric_bytes_value_tag;
            metric->value.string_value.funcs.encode = _pb_encode_bytes_callback;
            metric->value.string_value.arg = (void*)(value->value.bytesValue);
            break;
        default:
            metric->has_is_null = true;
            metric->is_null = true;
            break;
    }
}


static bool _pb_encode_metrics_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    // arg is an array of 2 bools
    bool *flags = *(bool **)arg; // Recasting and dereferencing
    bool birth = flags[0];
    bool is_historical = flags[1];

    for (size_t i = 0; i < getTagsCount(); i++) {
        FunctionalBasicTag* tag_ptr = getTagByIdx(i);
        if (!birth) {
            // Skip encode if RBE and value hasn't changed or if the tag alias is in ignored range
            if (!(tag_ptr->valueChanged) || tag_ptr->alias < -999) continue;
        }
        
        Payload_Metric metric = Payload_Metric_init_zero;

        // Check if historical is required
        if (is_historical) {
            metric.has_is_historical = true;
            metric.is_historical = true;
        }   
        
        bool include_name = birth || tag_ptr->alias < 0;
        bool include_alias = tag_ptr->alias > -1;
        // alias included for both data and birth
        if (include_alias) {
            // Ignore negative aliases, it's reserved alias for variables like Node Control/Scan Rate, etc
            metric.has_alias = true;
            metric.alias = tag_ptr->alias;
        }

        _basic_value_to_metric(&(tag_ptr->currentValue), &metric);

        if (include_name) {
            // name includes name only
            metric.name.funcs.encode = _pb_encode_string_callback;
            metric.name.arg = (void*)tag_ptr->name;
        }

        if (!birth) {
            // If it's not a birth payload, it's ready to encode
            if (!pb_encode_tag_for_field(stream, field)) return false;
            if (!pb_encode_submessage(stream, Payload_Metric_fields, &metric)) return false;
            continue;
        }

        // Include properties in birth payload
        Payload_PropertySet tag_properties = Payload_PropertySet_init_zero;

        tag_properties.keys.funcs.encode = _pb_encode_property_names;
        char* property_key_array[] = {"readOnly", NULL}; 
        tag_properties.keys.arg = property_key_array;

        tag_properties.values.funcs.encode = _pb_encode_property_values;

        //tag_properties.values.funcs.arg = &read_only;
        Payload_PropertyValue property_value = Payload_PropertyValue_init_zero;
        property_value.has_type = true;
        property_value.type = (uint32_t)11;  // boolean datatype code
        property_value.which_value = Payload_PropertyValue_boolean_value_tag;
        property_value.value.boolean_value = !(tag_ptr->remote_writable);
        Payload_PropertyValue* property_values[] = {&property_value, NULL};
        tag_properties.values.arg = (void*)property_values;
        
        metric.has_properties = true;
        metric.properties = tag_properties;

        // finally encode the metric
        if (!pb_encode_tag_for_field(stream, field)) return false;
        if (!pb_encode_submessage(stream, Payload_Metric_fields, &metric)) return false;
    }

    return true;
}


/*
Nanopb decode functions
*/

static bool _default_validate_scan_rate(BasicValue* newValue) {
    if (newValue == NULL) return false;
    if (newValue->isNull) return false;
    if (newValue->value.uint32Value < _SCAN_RATE_MIN || newValue->value.uint32Value > _SCAN_RATE_MAX) return false;
    return true;
}


static int _on_decode_metric_default(BasicValue* valueReceived, FunctionalBasicTag* matchedTag) {
    if (writeBasicTag(matchedTag, valueReceived)) return 0;
    return 1;
}


static bool _decode_string_callback(pb_istream_t *stream, const pb_field_iter_t *field, void **arg) {
    size_t str_length = stream->bytes_left;
    // Make Hard limit for incoming string length
    if (str_length > _INCOMING_STRING_MAX_LEN) return false;
    uint8_t* char_buf = malloc(str_length + 1); // memory leak potential
    if (char_buf == NULL) return false;
    char_buf[str_length] = '\0';
    if (!pb_read(stream, char_buf, str_length)) {
        free(char_buf);
        return false;
    }
    *arg = char_buf;
    return true;
}


static bool _decode_buffer_callback(pb_istream_t *stream, const pb_field_iter_t *field, void **arg) {
    /*
    
    */
    size_t bytes_length = stream->bytes_left;
    // Make Hard limit for incoming string length
    if (bytes_length > _INCOMING_BUFFER_MAX_LEN) return false;

    BasicValue* value_ptr = (BasicValue*)(*arg);
    // Include an extra byte for null terminator, incase the buffer later needs to be recast as char*
    if (!allocateBufferValue(value_ptr, bytes_length + 1)) return false;  // memory leak potential
    value_ptr->value.bytesValue->allocated_length = bytes_length + 1;
    // preset the null char, it affects nothing if the value is bufferval, but ready to be recast as char*
    value_ptr->value.bytesValue->buffer[bytes_length] = '\0';
    if (!pb_read(stream, value_ptr->value.bytesValue->buffer, bytes_length)) {
        deallocateBufferValue(value_ptr);
        return false;
    }

    value_ptr->value.bytesValue->written_length = bytes_length;
    return true;
}


static bool _decode_metric_callback(pb_istream_t *stream, const pb_field_iter_t *field, void **arg) {
    Payload_Metric metric = Payload_Metric_init_zero;
    
    metric.name.funcs.decode = _decode_string_callback;
    metric.name.arg = NULL;

    // set decode in case the metric is string or bytes
    metric.value.bytes_value.funcs.decode = _decode_buffer_callback;
    BasicValue metric_value;
    metric_value.value.bytesValue = NULL;

    metric.value.bytes_value.arg = &metric_value;

    if (!pb_decode(stream, Payload_Metric_fields, &metric)) {
        if (metric.name.arg != NULL) free(metric.name.arg);
        if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
        return false;
    }

    //if (!_sp_datatype_valid())

    FunctionalBasicTag* matchedTag = NULL;
    if (metric.has_alias) {
        // Get tag by alias
        matchedTag = getTagByAlias((int)metric.alias);
    } else if (metric.name.arg != NULL) {
        // If there was no alias, check and find it by name
        matchedTag = getTagByName((char*)(metric.name.arg));
    }

    // free the name, no longer needed
    if (metric.name.arg != NULL) {
        free(metric.name.arg);
        metric.name.arg = NULL;
    }
    
    if (matchedTag == NULL) {
        // No tag found, ignore this metric, but decode was successful
        if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
        return true;
    } else if (!(matchedTag->remote_writable)) {
        // Tag is not writable via NCMD, ignore it
        if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
        return true;
    }

    // if incoming metric datatype does not match tag, ignore it
    if (metric.datatype != matchedTag->datatype) {
        // Ignition (Java) sends uint64 as int64, so make exception for that scenario
        if (matchedTag->datatype != spUInt64 && metric.datatype != (int)spInt64) {
            if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
            return true;
        }
    }

    metric_value.datatype = matchedTag->datatype;
    metric_value.timestamp = metric.timestamp;

    if (metric.has_is_null && metric.is_null) {
        // Value is null
        metric_value.isNull = true;
    } else {
        metric_value.isNull = false;
        switch (metric_value.datatype) {
            case spInt8:
                metric_value.value.int8Value = (int8_t)(metric.value.int_value);
                break;
            case spInt16:
                metric_value.value.int16Value = (int16_t)(metric.value.int_value);
                break;
            case spInt32:
                metric_value.value.int32Value = (int32_t)(metric.value.int_value);
                break;
            case spInt64:
                metric_value.value.int64Value = (int64_t)(metric.value.long_value);
                break;
            case spUInt8:
                metric_value.value.uint8Value = (uint8_t)(metric.value.int_value);
                break;
            case spUInt16:
                metric_value.value.uint16Value = (uint16_t)(metric.value.int_value);
                break;
            case spUInt32:
                metric_value.value.uint32Value = (uint32_t)(metric.value.int_value);
                break;
            case spDateTime:
            case spUInt64:
                metric_value.value.uint64Value = (uint64_t)(metric.value.long_value);
                break;
            case spFloat:
                metric_value.value.floatValue = metric.value.float_value;
                break;
            case spDouble:
                metric_value.value.doubleValue = metric.value.double_value;
                break;
            case spBoolean:
                metric_value.value.boolValue = metric.value.boolean_value;
                break;
            case spText:  // Text is a string
            case spUUID: // UUID is a string
            case spString:
                // Handle String Differently
                {
                    // save the ptr to not lose it
                    BufferValue* buffer_ptr = metric_value.value.bytesValue;
                    // recast the buffer to char*, reset null terminator
                    buffer_ptr->buffer[buffer_ptr->allocated_length - 1] = '\0';
                    metric_value.value.stringValue = (char*)(buffer_ptr->buffer);
                    // free the BufferValue
                    free(buffer_ptr);
                    break;
                }
            case spBytes:
                // Bytes is already set
                break;
            default:
                // Datatype is invalid or unimplimented
                if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
                return true; // decode successful, but the metric is ignored
        }
    }

    // Call the callback
    DecodeMetricCallback callback = *(DecodeMetricCallback*)arg;
    if (callback != NULL) {
        callback(&metric_value, matchedTag);
    } else {
        _on_decode_metric_default(&metric_value, matchedTag);
    }

    // Cleanup any allocations
    switch (metric_value.datatype) {
        case spText:  // Text is a string
        case spUUID: // UUID is a string
        case spString:
            // Deallocate String
            if (metric_value.value.stringValue != NULL) free(metric_value.value.stringValue);
            break;
        case spBytes:
            // Deallocate Bytes
            if (metric_value.value.bytesValue != NULL) deallocateBufferValue(&metric_value);
            break;
        default:
            // Nothing to deallocate
            break;
    }
    return true;
}


static bool _decode_payload(const uint8_t *payload_buf, size_t length, Payload *decoded_payload, DecodeMetricCallback onMetricCallback) {
    pb_istream_t stream = pb_istream_from_buffer(payload_buf, length);

    decoded_payload->metrics.funcs.decode = _decode_metric_callback;
    if (onMetricCallback != NULL) decoded_payload->metrics.arg = onMetricCallback;
    bool status = pb_decode(&stream, Payload_fields, decoded_payload);
    return status;
}


/*
Sparkplug Functions
*/

static bool _make_ndeath_payload(BufferValue* buffer_ptr, StreamFunction streamFn, uint64_t timestamp) {
    // Get the bdSeq Tag
    FunctionalBasicTag* bdSeq_tag = getTagByName("bdSeq");
    if (bdSeq_tag == NULL || !_NODE_INITIALIZED) return false;  // bdSeq doesn't exist, can't make ndeath payload

    Payload payload = Payload_init_zero;

    payload.has_timestamp = true;
    payload.timestamp = timestamp;

    Payload_Metric metric = Payload_Metric_init_zero;
    metric.name.funcs.encode = _pb_encode_string_callback;
    metric.name.arg = "bdSeq";

    _basic_value_to_metric(&(bdSeq_tag->currentValue), &metric);

    // override timestamp value
    metric.timestamp = payload.timestamp;

    payload.metrics.funcs.encode = _pb_encode_single_metric_callback;
    payload.metrics.arg = &metric;

    return _encode_payload(&payload, buffer_ptr, streamFn);
}


static bool _make_metrics_payload(BufferValue* buffer_ptr, StreamFunction streamFn, uint64_t timestamp, int sequence, bool isBirth, bool isHistorical) {
    if (!_NODE_INITIALIZED) return false;

    Payload payload = Payload_init_zero;
    payload.has_timestamp = true;
    payload.timestamp = timestamp;
    payload.has_seq = true;
    payload.seq = sequence;

    bool flags[2];
    flags[0] = isBirth;
    flags[1] = isHistorical;
    payload.metrics.funcs.encode = _pb_encode_metrics_callback;
    payload.metrics.arg = (void*)(&flags);

    return _encode_payload(&payload, buffer_ptr, streamFn);
}


bool encodePayloadToStream(Payload* payload, StreamFunction streamFn) {
    return _encode_payload(payload, NULL, streamFn);
}

bool encodePayloadToBuffer(Payload* payload, BufferValue* buffer) {
    return _encode_payload(payload, buffer, NULL);
}


bool makeNDEATH(uint64_t timestamp) {
    return _make_ndeath_payload(_ENCODE_BUFFER, _ENCODE_STREAM, timestamp);
}

bool makeNBIRTH(uint64_t timestamp, int sequence) {
    return _make_metrics_payload(_ENCODE_BUFFER, _ENCODE_STREAM, timestamp, sequence, true, false);
}

bool makeHistoricalNBIRTH(uint64_t timestamp, int sequence) {
    return _make_metrics_payload(_ENCODE_BUFFER, _ENCODE_STREAM, timestamp, sequence, true, true);
}

bool makeNDATA(uint64_t timestamp, int sequence) {
    return _make_metrics_payload(_ENCODE_BUFFER, _ENCODE_STREAM, timestamp, sequence, false, false);
}

bool makeHistoricalNDATA(uint64_t timestamp, int sequence) {
    return _make_metrics_payload(_ENCODE_BUFFER, _ENCODE_STREAM, timestamp, sequence, false, true);
}

bool processNCMD(uint8_t* buffer, size_t length, DecodeMetricCallback metric_callback) {
    /*
    Decode and write NCMD to tags
    */
   Payload decoded_payload = Payload_init_zero;

   bool result = _decode_payload(buffer, length, &decoded_payload, metric_callback);

   return result;
}

// Init Functions

bool setEncodeStream(StreamFunction streamFn) {
    if (streamFn == NULL) return false;
    _ENCODE_STREAM = streamFn;
    return true;
}

bool setEncodeBuffer(BufferValue* bufferVal) {
    if (bufferVal == NULL) return false;
    _ENCODE_BUFFER = bufferVal;
    return true;
}

static int64_t _get_bdseq_default() {
    // To be expanded, load from flash memory, etc
    return 0;
}

static int64_t _get_scan_rate_default() {
    // To be expanded, load from flash memory, etc
    return 1000;
}

static bool _abort_tags_init(void* ptr_to_check, void* ptr_to_free) {
    /* used to check if a pointer has been allocated or not */
    if (ptr_to_check == NULL) {
        if (ptr_to_free != NULL) free(ptr_to_free);
        deleteSparkplugTags();
        return true;
    }
    return false;
}


bool initializeSparkplugTags(BufferValue* bufferVal, StreamFunction streamFn) {
    /*
    Create the tags needed for a sparkplug node, bdSeq, Node Control/Rebirth, etc
    Set alias to negative number, this library ignores all tags with negative alias in RBE,
    and doesn't include the alias in Birth payloads
    */
    if (_NODE_INITIALIZED) return false;

    if (bufferVal != NULL) {
        if (!setEncodeBuffer(bufferVal)) return false;
    } else if (streamFn != NULL) {
        if (!setEncodeStream(streamFn)) return false;
    } else if (_ENCODE_BUFFER == NULL && _ENCODE_STREAM == NULL) {
        // No stream or buffer supplied, none is set either
        return false;
    }

    // Check if tags already exist for memory safety
    FunctionalBasicTag* bdSeqTag = getTagByName(_bdseq_tag_name);
    FunctionalBasicTag* rebirthTag = getTagByName(_rebirth_tag_name);
    FunctionalBasicTag* scanRateTag = getTagByName(_scan_rate_tag_name);
    if (bdSeqTag != NULL || rebirthTag != NULL || scanRateTag != NULL) return false;
     
    int64_t* bdSeq_value = (int64_t*)malloc(sizeof(int64_t));
    if (_abort_tags_init(bdSeq_value, NULL)) return false;
    *bdSeq_value = _get_bdseq_default();
    bdSeqTag = createInt64Tag(_bdseq_tag_name, bdSeq_value, _bdseq_tag_alias, false, false);
    if (_abort_tags_init(bdSeqTag, bdSeq_value)) return false;


    // Create Node Control/Rebirth
    bool* rebirth_value = (bool*)malloc(sizeof(bool));
    if (_abort_tags_init(rebirth_value, NULL)) return false;
    *rebirth_value = false;
    rebirthTag = createBoolTag(_rebirth_tag_name, rebirth_value, _rebirth_tag_alias, false, true);
    if (_abort_tags_init(rebirthTag, rebirth_value)) return false;


    // Create Scan Rate
    int64_t* scan_rate_value = (int64_t*)malloc(sizeof(int64_t));
    if (_abort_tags_init(scan_rate_value, NULL)) return false;
    *scan_rate_value = _get_scan_rate_default();
    scanRateTag = createInt64Tag(_scan_rate_tag_name, scan_rate_value, -901, false, true);
    if (_abort_tags_init(scanRateTag, scan_rate_value)) return false;
    scanRateTag->validateWrite = _default_validate_scan_rate;

    _NODE_INITIALIZED = true;
    return true;
}


bool deleteSparkplugTags() {
    if (!_NODE_INITIALIZED) return false;
    // deallocate the tag's value_address, then call deleteBasicTag
    FunctionalBasicTag* bdSeqTag = getTagByName(_bdseq_tag_name);
    if (bdSeqTag != NULL) {
        free(bdSeqTag->value_address);
        deleteTag(bdSeqTag);
    }
    FunctionalBasicTag* rebirthTag = getTagByName(_rebirth_tag_name);
    if (rebirthTag != NULL) {
        free(rebirthTag->value_address);
        deleteTag(rebirthTag);
    }
    FunctionalBasicTag* scanRateTag = getTagByName(_scan_rate_tag_name);
    if (scanRateTag != NULL) {
        free(scanRateTag->value_address);
        deleteTag(scanRateTag);
    }
    _NODE_INITIALIZED = false;
    return true;
}


bool sparkplugInitialized() {
    return _NODE_INITIALIZED;
}

// Special getTag functions
FunctionalBasicTag* getBdSeqTag() {
    return getTagByName(_bdseq_tag_name);
}
FunctionalBasicTag* getRebirthTag() {
    return getTagByName(_rebirth_tag_name);
}
FunctionalBasicTag* getScanRateTag() {
    return getTagByName(_scan_rate_tag_name);
}


// Sparkplug Event Action Functions




