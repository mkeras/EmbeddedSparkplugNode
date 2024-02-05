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

#ifndef SPARKPLUG_NODE_H
#define SPARKPLUG_NODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <BasicTag.h>
#include "EmbeddedSparkplugPayloads.h"

/* For future version
typedef struct SparkplugMQTTBrokerDetails {
    const char* host;
    uint16_t port;
    const char* client_id;
    const char* username;
    const char* password;
    struct SparkplugMQTTBrokerDetails* next_broker;
} SparkplugMQTTBrokerDetails;*/

typedef struct SparkplugNodeConfig SparkplugNodeConfig;
typedef struct SparkplugMQTTMessage SparkplugMQTTMessage;

struct SparkplugMQTTMessage {
    const char* topic;
    BufferValue* payload;
}; 


struct SparkplugNodeConfig {
    const char* node_id;
    const char* group_id;
    const char* tags_group;  // For future version of BasicTag
    BufferValue payload_buffer;
    TimestampFunction timestamp_function;
    struct Topics {
        const char* NCMD;
        const char* NBIRTH;
        const char* NDEATH;
        const char* NDATA;
    } topics;
    struct SparkplugTags {
        FunctionalBasicTag* rebirth;
        FunctionalBasicTag* scan_rate;
        FunctionalBasicTag* bd_seq;
    } node_tags;
    struct Variables {
        bool* rebirth_tag_value;
        int64_t* scan_rate_tag_value;
        int64_t* bd_seq_tag_value;
        uint64_t last_scan;
        bool force_scan;
        bool values_changed;
        uint8_t sequence;
        bool initial_birth_made;
        bool mqtt_connected;
    } vars;
    SparkplugMQTTMessage mqtt_message;
};


typedef enum {
    spn_ERROR_NODE_NULL = -1,
    spn_SCAN_NOT_DUE = 0,
    spn_SCAN_FAILED = 1,
    spn_MAKE_NBIRTH_FAILED = 2,
    spn_NBIRTH_PL_READY = 3,
    spn_VALUES_UNCHANGED = 4,
    spn_MAKE_NDATA_FAILED = 5,
    spn_NDATA_PL_READY = 6,
    spn_MAKE_NDEATH_FAILED = 7,
    spn_NDEATH_PL_READY = 8,
    spn_PROCESS_NCMD_FAILED = 9,
    spn_PROCESS_NCMD_SUCCESS = 10,
    spn_HISTORICAL_NBIRTH_PL_READY = 11,
    spn_HISTORICAL_NDATA_PL_READY = 12
} SparkplugNodeState;




/*
Initializer functions
*/
SparkplugNodeConfig* createSparkplugNode(const char* group_id, const char* node_id, size_t payload_buffer_size, TimestampFunction timestamp_function);

bool deleteSparkplugNode(SparkplugNodeConfig* sparkplug_node);


/*
Node Functions
*/

bool scanDue(SparkplugNodeConfig* node);

bool scanTags(SparkplugNodeConfig* node);


SparkplugNodeState makeNDEATHPayload(SparkplugNodeConfig* node);

SparkplugNodeState tickSparkplugNode(SparkplugNodeConfig* node);

SparkplugNodeState processIncomingNCMDPayload(SparkplugNodeConfig* node, uint8_t* buffer, size_t length);

/*
Sparkplug Events
*/
void spnOnMQTTConnected(SparkplugNodeConfig* node);

void spnOnMQTTDisconnected(SparkplugNodeConfig* node);

void spnOnPublishNBIRTH(SparkplugNodeConfig* node);

void spnOnPublishNDATA(SparkplugNodeConfig* node);


#ifdef __cplusplus
}
#endif
#endif // SPARKPLUG_NODE_H