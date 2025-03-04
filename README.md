# EmbeddedSparkplugNode Library Documentation

## V0.2.4
- Added readOnly property to Birth payloads. readOnly set to true when a tag's remote_writable is false, and vice versa.
- Future version will have the option to add custom properties to tags.

## V0.2.3
- Minor Bug Fixes.
- Updated supported architectures to exclude AVR boards for the time being, as there are some issues with larger datatypes (double, uint64_t, etc). Undecided on solution for this issue (create seperate library for AVR/8 bit controllers or fully implement AVR compatibility).

## Overview
This library is written in C and builds on top of the [BasicTag](https://github.com/mkeras/BasicTag) library to provide an easy to use API for creating tags, monitoring tags for changes, and enabling a microcontroller to operate as an Edge of Network Node according to the [Sparkplug Specification](https://sparkplug.eclipse.org/specification/version/3.0/documents/sparkplug-specification-3.0.0.pdf). This library is designed to perform all the neccessary operations of a Sparkplug Edge Node, but decoupled from any MQTT Library or other platform specific dependency for the purpose of maximum platform support. For the protobuf encoding/decoding operations, this library uses code generated by [nanoPB](https://jpa.kapsi.fi/nanopb/).


## Notes
- This library has not yet been thoroughly tested, it can be considered alpha, and is currently just a hobby project. Any feedback for bugs, suggestions, etc is appreciated.
- As noted in the overview, this library contains all the components to operate an edge of network node, but will need to be combined with an appropriate MQTT client library by the user. A full demonstration of this will be added to the example Arduino sketches in the near future.
- Sparkplug B, MQTT, and other related terms are trademarks of their respective owners. Any use of these terms in this project is for descriptive purposes only and does not imply any endorsement or affiliation.
- The current purpose of this library is to provide an open-source implementation of the Sparkplug B MQTT payload standard for hobbyist, educational and development uses.
- The documentation is not yet complete, further API documentation and examples will be added in the near future. The provided example demonstrates the core function of the library.


## Data Structures

### `SparkplugNodeConfig`
```c
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
        uint32_t* scan_rate_tag_value;
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
```

Typdef struct that holds configuration and state data for the Sparkplug node. Its values will generally not be interacted with directly in standard use, instead the API functions are be called on it to perform neccessary node operations (see the example Arduino sketch). BufferValue and TimestampFunction are defined by the [BasicTag library](https://github.com/mkeras/BasicTag). The API functions all require a `SparkplugNodeConfig*` pointer in their arguments.

- **`payload_buffer`**: `BufferValue` - Allocated buffer for storing encoded payloads.
- **`timestamp_function`**: `TimestampFunction` - The TimestampFunction provided to the createSparkplugNode function, returns a uint64_t timestamp, representing epoch timestamp in milliseconds.
- **`node_id`**: `const char*` - The Sparkplug node id.
- **`group_id`**: `const char*` - The Sparkplug grouo id.
- **`topics`**: `struct` - Allocated chars for each topic (NDATA, NBIRTH, NDEATH, NCMD).
- **`node_tags`**: `struct` - Pointers to the [FunctionBasicTag](https://github.com/mkeras/BasicTag) tags associated with the node (bdSeq, Rebirth, Scan Rate).
- **`vars`**: `struct` - Internal state variables including tag values and MQTT connection state, last scan, etc. They are managed and modified by the API Functions
- **`mqtt_message`**: `struct` - Where the topic and encoded payload are stored by the API functions when a payload is successfully encoded.


### `SparkplugNodeState`
```c
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
    spn_PROCESS_NCMD_SUCCESS = 10
} SparkplugNodeState;
```

Enumerates possible states of a Sparkplug node operation. Useful for creating switch statements for clear actions based on certain states. The values follow the order of operations of the `tickSparkplugNode` function (The `tickSparkplugNode` doesn't go past `spn_NDATA_PL_READY`; it doesn't handle making NDEATH or NCMD as those are triggered externally of normal operation).

- **`spn_ERROR_NODE_NULL`**: Node configuration is null. Should not happen when using API functions, unless initialization failed due to memory constraints.
- **`spn_SCAN_NOT_DUE`**: No scan is due based on the current scan rate.
- **`spn_SCAN_FAILED`**: Attempted to scan tags, but the scan failed. Currently this only would happen if the `SparkplugNodeConfig*` pointer was NULL.
- **`spn_MAKE_NBIRTH_FAILED`**: Attempted to create an NBIRTH payload, but encoding failed.
- **`spn_NBIRTH_PL_READY`**: An NBIRTH payload was created and available at the mqtt_message of the node config to be immediately published.
- **`spn_VALUES_UNCHANGED`**: Report By Exception; values have not changed since last scan, so no further action required.
- **`spn_MAKE_NDATA_FAILED`**: Attempted to create an NDATA payload, but encoding failed.
- **`spn_NDATA_PL_READY`**: An NDATA payload was created and available at the mqtt_message of the node config to be immediately published.
- **`spn_MAKE_NDEATH_FAILED`**: Attempted to create an NDEATH payload, but encoding failed. Returned by the `makeNDEATHPayload` function.
- **`spn_NDEATH_PL_READY`**: An NDEATH payload was created and available at the mqtt_message of the node config to be included in the MQTT Connect operation. Returned by the `makeNDEATHPayload` function.
- **`spn_PROCESS_NCMD_FAILED`**: Attempted to process an incoming NCMD payload, but decoding failed. Note that the payload could have partially decoded and written values, as the incoming metrics are written to tags on the fly. Returned by the `processIncomingNCMDPayload` function.
- **`spn_PROCESS_NCMD_SUCCESS`**: Successfully processed an incoming NCMD. Returned by the `processIncomingNCMDPayload` function.


## API Documentation
This library is built on top of the [BasicTag library](https://github.com/mkeras/BasicTag), so understanding its documentation is key to understanding this library. The create tags functions of BasicTag are what the user of this library uses to create tags for the sparkplug node to report on.


### `createSparkplugNode`
```cpp
SparkplugNodeConfig* createSparkplugNode(const char* group_id, const char* node_id, size_t  payload_buffer_size, TimestampFunction timestamp_function);
```
Initializes and allocates a SparkplugNodeConfig struct and returns it's pointer. Handles the creation of bdSeq, Rebirth, and Scan Rate tags. If it returns a non NULL pointer, it has successfully initialized, and is ready to use with the rest of the API functions. Requires a group id, node id, buffer size for encoded payloads, and a timestamp function for getting millisecond timestamps.


### `create<type>Tag`
Used for creating tags for the node to report on. They receive a pointer to a value to monitor, and neccessary metadata to create a tag. They are the create tag functions of the BasicTag library. See the [BasicTag documentation](https://github.com/mkeras/BasicTag) for details.


### Event Callbacks
These are callbacks for external events to call in order for the node state to be correctly maintained. For these 4, their names explain it all, they are simply to be called when the corresponding events occur:
```c
void spnOnMQTTConnected(SparkplugNodeConfig* node);
void spnOnMQTTDisconnected(SparkplugNodeConfig* node);
void spnOnPublishNBIRTH(SparkplugNodeConfig* node);
void spnOnPublishNDATA(SparkplugNodeConfig* node);
```

This callback function is called when an incoming NCMD message is received and returns a SparkplugNodeState:
```c
SparkplugNodeState processIncomingNCMDPayload(SparkplugNodeConfig* node, uint8_t* buffer, size_t length);
```
This function is meant to be called by the handler/callback for an incoming MQTT message. For example, with pubsub client you could use the following wrapper and then set the callback on your pubsubclient:
```cpp
void NCMDcallback(const char[] topic, byte* payload, unsigned int length) {
    SparkplugNodeState result = processIncomingNCMDPayload(yourSparkplugNodeConfigInstanceName, payload, length);

    // additional logic to handle result (success or failure), etc.

}
```
Something like this would then be in your setup code section:
```cpp
yourPubsubclientInstanceName.setCallback(NCMDcallback);
```
And in your MQTT on connect handler something like this:
```cpp
yourPubsubclientInstanceName.subscribe(yourSparkplugNodeConfigInstanceName->topics.NCMD);
```

<br><br>
This is not technically a callback, but it is called just before connecting to an mqtt broker to supply the death payload and topic to the broker:
```c
SparkplugNodeState makeNDEATHPayload(SparkplugNodeConfig* node);
```


### `tickSparkplugNode`
```c
SparkplugNodeState tickSparkplugNode(SparkplugNodeConfig* node);
```
This is the main function to call in the loop of a program/sketch it returns a SparkplugNodeState, incdicating what the next action should be, if any. Combined with the above callbacks, it enables the functionality of the library. Check the example Arduino sketch to see a full demo.


### Accessing the `node->mqtt_message`
The mqtt_message member struct of the `SparkplugNodeConfig` is designed to be a helpful access point for passing a payload and topic to an MQTT publish function. The 3 relevant members are accessed like this:
```c
// node is a SparkplugNodeConfig pointer (SparkplugNodeConfig*)

const char* topic = node->mqtt_message.topic; // the topic
uint8_t* payload = node->mqtt_message.payload->buffer; // the buffer where the payload is stored
size_t payload_size = node->mqtt_message.payload->written_length; // the length (bytes) of the payload
```


### Additional API Functions
There are several additional API functions that are not included in this version of the documentation. It is planned to add in the near future, but they aren't neccessary for simple usage of this library.