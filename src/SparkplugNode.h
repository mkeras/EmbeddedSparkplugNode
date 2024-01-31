/*
Copyright 2024 Michael Keras

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

typedef struct SparkplugNodeConfig SparkplugNodeConfig; // Forward declaration

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
        uint32_t* scan_rate_tag_value;
        int64_t* bd_seq_tag_value;
        uint64_t last_scan;
        bool force_scan;
        bool values_changed;
        uint8_t sequence;
    } vars;
};



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

/*
Sparkplug Events
*/



#ifdef __cplusplus
}
#endif
#endif // SPARKPLUG_NODE_H