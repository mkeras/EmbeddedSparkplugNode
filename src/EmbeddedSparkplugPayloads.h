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

#ifndef EMBEDDED_SPARKPLUG_PAYLOADS_H
#define EMBEDDED_SPARKPLUG_PAYLOADS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pb_encode.h"
#include "pb_decode.h"
#include "sparkplug.pb.h"
#include <BasicTag.h>


typedef void (*StreamFunction)(uint8_t* byte_ptr, size_t length);
typedef int (*DecodeMetricCallback)(BasicValue* valueReceived, FunctionalBasicTag* matchedTag); // for custom behaviour

// Wrapper for FunctionalBasicTag to hold tag specific encode/decode config
// Potentially added in v1.1.0
/*typedef struct SparkplugTagData SparkplugTagData;
struct SparkplugTagData {
    DecodeMetricCallback on_cmd_callback;
};*/

int encodeDataPayload(BufferValue* buffer);

int encodeBirthPayload(BufferValue* buffer);

int encodeDeathPayload(BufferValue* buffer);

int decodePayload(BufferValue* buffer);


// These functions are for custom payloads or custom stream/buffer
bool encodePayloadToStream(Payload* payload, StreamFunction streamFn);
bool encodePayloadToBuffer(Payload* payload, BufferValue* buffer);


// Init Functions

bool setEncodeStream(StreamFunction streamFn);
bool setEncodeBuffer(BufferValue* bufferVal);

bool initializeSparkplugTags(BufferValue* bufferVal, StreamFunction streamFn);
bool deleteSparkplugTags(); // Deallocate the tags
bool sparkplugInitialized();

// Special getTag functions

FunctionalBasicTag* getBdSeqTag();
FunctionalBasicTag* getRebirthTag();
FunctionalBasicTag* getScanRateTag();

// Sparkplug Event Action Functions

bool makeNDEATH(uint64_t timestamp);
bool makeNBIRTH(uint64_t timestamp, int sequence);
bool makeHistoricalNDATA(uint64_t timestamp, int sequence);
bool makeNDATA(uint64_t timestamp, int sequence);
bool makeHistoricalNDATA(uint64_t timestamp, int sequence);

// decode functions

bool processNCMD(uint8_t* buffer, size_t length, DecodeMetricCallback metric_callback);



#ifdef __cplusplus
}
#endif
#endif // EMBEDDED_SPARKPLUG_PAYLOADS_H