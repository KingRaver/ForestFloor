#ifndef FF_ABI_CONTRACTS_H_
#define FF_ABI_CONTRACTS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FF_ABI_VERSION_MAJOR = 1,
  FF_ABI_VERSION_MINOR = 0,
};

enum {
  FF_EVENT_TYPE_NOTE_ON = 1,
  FF_EVENT_TYPE_NOTE_OFF = 2,
  FF_EVENT_TYPE_TRIGGER = 3,
  FF_EVENT_TYPE_TRANSPORT_START = 4,
  FF_EVENT_TYPE_TRANSPORT_STOP = 5,
};

typedef struct ff_note_event_t {
  uint8_t track_index;
  uint8_t note;
  uint16_t reserved;
  float velocity;
} ff_note_event_t;

typedef struct ff_trigger_event_t {
  uint8_t track_index;
  uint8_t step_index;
  uint16_t reserved;
  float velocity;
} ff_trigger_event_t;

typedef struct ff_transport_event_t {
  float bpm;
} ff_transport_event_t;

typedef union ff_event_payload_t {
  ff_note_event_t note;
  ff_trigger_event_t trigger;
  ff_transport_event_t transport;
} ff_event_payload_t;

typedef struct ff_event_t {
  uint64_t timeline_sample;
  uint32_t block_offset;
  uint16_t source_id;
  uint16_t reserved;
  uint32_t event_type;
  ff_event_payload_t payload;
} ff_event_t;

typedef struct ff_parameter_update_t {
  uint32_t parameter_id;
  float normalized_value;
  uint32_t ramp_samples;
  uint32_t reserved;
} ff_parameter_update_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FF_ABI_CONTRACTS_H_

