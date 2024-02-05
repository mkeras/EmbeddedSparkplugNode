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

#include "SparkplugNode.h"

static const bool _USE_SPARKPLUG_3 = false;
static const char* _TOPIC_NAMESPACE = "spBv1.0";
static const size_t _TOPIC_NAMESPACE_LEN = 7;


const char* _make_topic_char(const char* group_id, const char* node_id, const char* topic_type) {
    size_t group_id_len = strlen(group_id);
    size_t node_id_len = strlen(node_id);
    size_t topic_type_len = strlen(topic_type);
    size_t char_size = _TOPIC_NAMESPACE_LEN + group_id_len + node_id_len + topic_type_len + 3; // The 3 is for 3 '/' chars
    char* newChar = (char*)malloc(char_size + 1); // extra for null terminator
    if (newChar == NULL) return NULL;
    size_t pos = 0;
    memcpy(&newChar[pos], _TOPIC_NAMESPACE, _TOPIC_NAMESPACE_LEN);
    pos += _TOPIC_NAMESPACE_LEN;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], group_id, group_id_len);
    pos += group_id_len;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], topic_type, topic_type_len);
    pos += topic_type_len;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], node_id, node_id_len);
    newChar[char_size] = '\0'; // Set the null terminator
    return (const char*)newChar;
}

static void* _get_tag_value_address(const char* tag_name) {
    FunctionalBasicTag* tag = getTagByName(tag_name);
    if (tag == NULL) return NULL;
    return tag->value_address;
}

SparkplugNodeConfig* createSparkplugNode(const char* group_id, const char* node_id, size_t payload_buffer_size, TimestampFunction timestamp_function) {
    if (node_id == NULL || group_id == NULL || payload_buffer_size == 0 || timestamp_function == NULL) return NULL;
    SparkplugNodeConfig* newNode = (SparkplugNodeConfig*)malloc(sizeof(SparkplugNodeConfig));
    if (newNode == NULL) return NULL;

    // set timestamp function
    setBasicTagTimestampFunction(timestamp_function);

    newNode->tags_group = NULL; // NOT CURRENTLY USED
    newNode->group_id = group_id;
    newNode->node_id = node_id;
    newNode->timestamp_function = timestamp_function;
    newNode->vars.last_scan = 0;
    newNode->vars.force_scan = false;
    newNode->vars.values_changed = false;
    newNode->vars.sequence = 0;
    newNode->vars.initial_birth_made = false;
    newNode->vars.mqtt_connected = false;

    newNode->mqtt_message.topic = NULL;
    newNode->mqtt_message.payload = NULL;

    newNode->topics.NCMD = _make_topic_char(group_id, node_id, "NCMD");
    if (newNode->topics.NCMD == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NBIRTH = _make_topic_char(group_id, node_id, "NBIRTH");
    if (newNode->topics.NBIRTH == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NDEATH = _make_topic_char(group_id, node_id, "NDEATH");
    if (newNode->topics.NDEATH == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NDATA = _make_topic_char(group_id, node_id, "NDATA");
    if (newNode->topics.NDATA == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    
    // Allocate the BufferValue
    newNode->payload_buffer.buffer = (uint8_t*)malloc(payload_buffer_size);
    if (newNode->payload_buffer.buffer == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->payload_buffer.allocated_length = payload_buffer_size;
    newNode->payload_buffer.written_length = 0;

    // initialize sparkplug tags
    if (sparkplugInitialized()) {
        // initialize must be done by this function
        deleteSparkplugNode(newNode);
        return NULL;
    }
    if (!initializeSparkplugTags(&(newNode->payload_buffer), NULL)) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    
    newNode->node_tags.bd_seq = getBdSeqTag();
    newNode->vars.bd_seq_tag_value = (int64_t*)(newNode->node_tags.bd_seq->value_address);
    
    newNode->node_tags.scan_rate = getScanRateTag();
    newNode->vars.scan_rate_tag_value = (int64_t*)(newNode->node_tags.scan_rate->value_address);

    newNode->node_tags.rebirth = getRebirthTag();
    newNode->vars.rebirth_tag_value = (bool*)(newNode->node_tags.rebirth->value_address);

    return newNode;
}


bool deleteSparkplugNode(SparkplugNodeConfig* sparkplug_node) {
    if (sparkplug_node == NULL) return false;

    // free the topics
    if (sparkplug_node->topics.NCMD != NULL) free(sparkplug_node->topics.NCMD);
    if (sparkplug_node->topics.NBIRTH != NULL) free(sparkplug_node->topics.NBIRTH);
    if (sparkplug_node->topics.NDEATH != NULL) free(sparkplug_node->topics.NDEATH);
    if (sparkplug_node->topics.NDATA != NULL) free(sparkplug_node->topics.NDATA);
    sparkplug_node->topics.NCMD = NULL;
    sparkplug_node->topics.NBIRTH = NULL;
    sparkplug_node->topics.NDEATH = NULL;
    sparkplug_node->topics.NDATA = NULL;

    // free the buffer
    if(sparkplug_node->payload_buffer.buffer != NULL) free(sparkplug_node->payload_buffer.buffer);
    sparkplug_node->payload_buffer.buffer = NULL;

    // delete the sparkplug tags
    deleteSparkplugTags();

    // finally, free the node itself
    free(sparkplug_node);
    return true;
}



/*
Node Functions
*/

// Flag that indecates if bdSeq should be incremented or not before making Birth/Death payload
//static bool _FLAG_BDSEQ_FOR_INCREMENT = false;

static void _increment_sequence(SparkplugNodeConfig* node) {
    if (node == NULL) return;
    node->vars.sequence += 1;
}


bool scanDue(SparkplugNodeConfig* node) {
    if (node == NULL) return false;
    if (node->vars.force_scan) {
        node->vars.force_scan = false;
        return true;
    }
    if (!(node->vars.last_scan)) return true;
    uint64_t difference = (node->timestamp_function)() - node->vars.last_scan;
    return difference >= *(node->vars.scan_rate_tag_value);
}

bool scanTags(SparkplugNodeConfig* node) {
    if (node == NULL) return false;
    node->vars.values_changed = readAllBasicTags();
    node->vars.last_scan = node->timestamp_function();
    return true;
}


static bool _make_nbirth_payload(SparkplugNodeConfig* node) {
    if (!_USE_SPARKPLUG_3) {
        node->vars.sequence = 0;
    }
    if (node->vars.mqtt_connected) return makeNBIRTH(node->timestamp_function(), node->vars.sequence);
    return makeHistoricalNBIRTH(node->timestamp_function(), node->vars.sequence);
}

static bool _make_ndata_payload(SparkplugNodeConfig* node) {
    // if (node == NULL) return false;
    if (node->vars.mqtt_connected) return makeNDATA(node->timestamp_function(), node->vars.sequence);
    return makeHistoricalNDATA(node->timestamp_function(), node->vars.sequence);
}

static void _increment_bdseq(int64_t* bdseq_ptr) {
    if (bdseq_ptr == NULL) return;

    if (*bdseq_ptr > 254) {
        // rollover value
        *bdseq_ptr = 0;
    } else {
        *bdseq_ptr += 1;
    }
    return;
}

SparkplugNodeState makeNDEATHPayload(SparkplugNodeConfig* node) {
    // if (node == NULL) return false;

    // check if it is initial connect or not
    if (node->vars.initial_birth_made) {
        // This is a new reconnect packet, increment bdSeq
        _increment_bdseq(node->vars.bd_seq_tag_value);
    }
    readBasicTag(node->node_tags.bd_seq, node->timestamp_function());

    if (makeNDEATH(node->timestamp_function())) {
        node->mqtt_message.payload = &(node->payload_buffer);
        node->mqtt_message.topic = node->topics.NDEATH;
        return spn_NDEATH_PL_READY;
    }
    node->mqtt_message.payload = NULL;
    node->mqtt_message.topic = NULL;
    return spn_MAKE_NDEATH_FAILED;
}


SparkplugNodeState tickSparkplugNode(SparkplugNodeConfig* node) {
    if (node == NULL) return spn_ERROR_NODE_NULL;
    if (!scanDue(node)) return spn_SCAN_NOT_DUE;

    // Scan Tags
    if (!scanTags(node)) {
        node->vars.last_scan = node->timestamp_function();
        return spn_SCAN_FAILED;
    }

    // Check if rebirth command is set or if initial birth has been made
    if (*(node->vars.rebirth_tag_value) || !node->vars.initial_birth_made) {
        // Reset rebirth tag to false
        *(node->vars.rebirth_tag_value) = false;
        readBasicTag(node->node_tags.rebirth, node->timestamp_function());

        // check if payload was made
        if (!_make_nbirth_payload(node)) {
            node->mqtt_message.payload = NULL;
            node->mqtt_message.topic = NULL;
            return spn_MAKE_NBIRTH_FAILED;
        }

        node->mqtt_message.payload = &(node->payload_buffer);
        node->mqtt_message.topic = node->topics.NBIRTH;
        if (node->vars.mqtt_connected) return spn_NBIRTH_PL_READY;
        return spn_HISTORICAL_NBIRTH_PL_READY;
    }

    if (!(node->vars.values_changed)) {
        node->mqtt_message.payload = NULL;
        node->mqtt_message.topic = NULL;
        return spn_VALUES_UNCHANGED;
    }

    if (!_make_ndata_payload(node)) {
        node->mqtt_message.payload = NULL;
        node->mqtt_message.topic = NULL;
        return spn_MAKE_NDATA_FAILED;
    }

    node->mqtt_message.payload = &(node->payload_buffer);
    node->mqtt_message.topic = node->topics.NDATA;
    if (node->vars.mqtt_connected) return spn_NDATA_PL_READY;
    return spn_HISTORICAL_NDATA_PL_READY;
}


SparkplugNodeState processIncomingNCMDPayload(SparkplugNodeConfig* node, uint8_t* buffer, size_t length) {
    // flag for immediate scan
    node->vars.force_scan = true;
    if (processNCMD(buffer, length, NULL)) return spn_PROCESS_NCMD_SUCCESS;
    return spn_PROCESS_NCMD_FAILED;
}


/*
Sparkplug Events
*/

static void _on_publish_payload(SparkplugNodeConfig* node) {
    node->vars.sequence++;
}


void spnOnMQTTConnected(SparkplugNodeConfig* node) {
    if (node == NULL) return;
    node->vars.mqtt_connected = true;
    if (node->vars.initial_birth_made) {
        // flag rebirth on next tick
        *(node->vars.rebirth_tag_value) = true;
    }
}

void spnOnMQTTDisconnected(SparkplugNodeConfig* node) {
    if (node == NULL) return;
    node->vars.mqtt_connected = false;
}


void spnOnPublishNBIRTH(SparkplugNodeConfig* node) {
    // check if initial_birth_made is set
    if (!node->vars.initial_birth_made) node->vars.initial_birth_made = true;
    _on_publish_payload(node);
}

void spnOnPublishNDATA(SparkplugNodeConfig* node) {
    _on_publish_payload(node);
}