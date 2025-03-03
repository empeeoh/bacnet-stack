/**
 * @file
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2007
 * @brief The Load Control Objects from 135-2004-Addendum e 
 * @copyright SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bacdcode.h"
#include "bacnet/datetime.h"
#include "bacnet/basic/object/lc.h"
#include "bacnet/basic/object/ao.h"
#include "bacnet/wp.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/sys/debug.h"

#define PRINTF debug_printf

/* number of demo objects */
#ifndef MAX_LOAD_CONTROLS
#define MAX_LOAD_CONTROLS 4
#endif

/*  indicates the current load shedding state of the object */
static BACNET_SHED_STATE Present_Value[MAX_LOAD_CONTROLS];

/* load control objects are required to support LEVEL */
typedef enum BACnetShedLevelType {
    BACNET_SHED_TYPE_PERCENT, /* Unsigned */
    BACNET_SHED_TYPE_LEVEL, /* Unsigned */
    BACNET_SHED_TYPE_AMOUNT /* REAL */
} BACNET_SHED_LEVEL_TYPE;

#define DEFAULT_VALUE_PERCENT 100
#define DEFAULT_VALUE_LEVEL 0
#define DEFAULT_VALUE_AMOUNT 0.0

/* The shed levels for the LEVEL choice of BACnetShedLevel
   that have meaning for this particular Load Control object. */
typedef struct {
    BACNET_SHED_LEVEL_TYPE type;
    union {
        unsigned level;
        unsigned percent;
        float amount;
    } value;
} BACNET_SHED_LEVEL;
/* indicates the desired load shedding */
static BACNET_SHED_LEVEL Requested_Shed_Level[MAX_LOAD_CONTROLS];
/* Indicates the amount of power that the object expects
   to be able to shed in response to a load shed request. */
static BACNET_SHED_LEVEL Expected_Shed_Level[MAX_LOAD_CONTROLS];
/* Indicates the actual amount of power being shed in response
   to a load shed request. */
static BACNET_SHED_LEVEL Actual_Shed_Level[MAX_LOAD_CONTROLS];

/* indicates the start of the duty window in which the load controlled
   by the Load Control object must be compliant with the requested shed. */
static BACNET_DATE_TIME Start_Time[MAX_LOAD_CONTROLS];
static BACNET_DATE_TIME End_Time[MAX_LOAD_CONTROLS];
static BACNET_DATE_TIME Current_Time;

/* indicates the duration of the load shed action,
   starting at Start_Time in minutes */
static uint32_t Shed_Duration[MAX_LOAD_CONTROLS];

/* indicates the time window used for load shed accounting in minutes */
static uint32_t Duty_Window[MAX_LOAD_CONTROLS];

/* indicates and controls whether the Load Control object is
   currently enabled to respond to load shed requests.  */
static bool Load_Control_Enable[MAX_LOAD_CONTROLS];

/* indicates when the object receives a write to any of the properties
   Requested_Shed_Level, Shed_Duration, Duty_Window */
static bool Load_Control_Request_Written[MAX_LOAD_CONTROLS];
/* indicates when the object receives a write to Start_Time */
static bool Start_Time_Property_Written[MAX_LOAD_CONTROLS];

/* optional: indicates the baseline power consumption value
   for the sheddable load controlled by this object,
   if a fixed baseline is used.
   The units of Full_Duty_Baseline are kilowatts.*/
static float Full_Duty_Baseline[MAX_LOAD_CONTROLS];

#define MAX_SHED_LEVELS 3
/* Represents the shed levels for the LEVEL choice of
   BACnetShedLevel that have meaning for this particular
   Load Control object. */
/* The elements of the array are required to be writable,
   allowing local configuration of how this Load Control
   object will participate in load shedding for the
   facility. This array is not required to be resizable
   through BACnet write services. The size of this array
   shall be equal to the size of the Shed_Level_Descriptions
   array. The behavior of this object when the Shed_Levels
   array contains duplicate entries is a local matter. */
static unsigned Shed_Levels[MAX_LOAD_CONTROLS][MAX_SHED_LEVELS];

/* represents a description of the shed levels that the
   Load Control object can take on.  It is the same for
   all the load control objects in this example device. */
static char *Shed_Level_Descriptions[MAX_SHED_LEVELS] = { "dim lights 10%",
    "dim lights 20%", "dim lights 30%" };

static float Shed_Level_Values[MAX_SHED_LEVELS] = { 90.0, 80.0, 70.0 };

/* These three arrays are used by the ReadPropertyMultiple handler */
static const int Load_Control_Properties_Required[] = { PROP_OBJECT_IDENTIFIER,
    PROP_OBJECT_NAME, PROP_OBJECT_TYPE, PROP_PRESENT_VALUE, PROP_STATUS_FLAGS,
    PROP_EVENT_STATE, PROP_REQUESTED_SHED_LEVEL, PROP_START_TIME,
    PROP_SHED_DURATION, PROP_DUTY_WINDOW, PROP_ENABLE, PROP_EXPECTED_SHED_LEVEL,
    PROP_ACTUAL_SHED_LEVEL, PROP_SHED_LEVELS, PROP_SHED_LEVEL_DESCRIPTIONS,
    -1 };

static const int Load_Control_Properties_Optional[] = { PROP_DESCRIPTION,
    PROP_FULL_DUTY_BASELINE, -1 };

static const int Load_Control_Properties_Proprietary[] = { -1 };

void Load_Control_Property_Lists(
    const int **pRequired, const int **pOptional, const int **pProprietary)
{
    if (pRequired) {
        *pRequired = Load_Control_Properties_Required;
    }
    if (pOptional) {
        *pOptional = Load_Control_Properties_Optional;
    }
    if (pProprietary) {
        *pProprietary = Load_Control_Properties_Proprietary;
    }

    return;
}

void Load_Control_Init(void)
{
    unsigned i, j;

    for (i = 0; i < MAX_LOAD_CONTROLS; i++) {
        /* FIXME: load saved data? */
        Present_Value[i] = BACNET_SHED_INACTIVE;
        Requested_Shed_Level[i].type = BACNET_SHED_TYPE_LEVEL;
        Requested_Shed_Level[i].value.level = 0;
        datetime_wildcard_set(&Start_Time[i]);
        datetime_wildcard_set(&End_Time[i]);
        datetime_wildcard_set(&Current_Time);
        Shed_Duration[i] = 0;
        Duty_Window[i] = 0;
        Load_Control_Enable[i] = true;
        Full_Duty_Baseline[i] = 1.500; /* kilowatts */
        Expected_Shed_Level[i].type = BACNET_SHED_TYPE_LEVEL;
        Expected_Shed_Level[i].value.level = 0;
        Actual_Shed_Level[i].type = BACNET_SHED_TYPE_LEVEL;
        Actual_Shed_Level[i].value.level = 0;
        Load_Control_Request_Written[i] = false;
        Start_Time_Property_Written[i] = false;
        for (j = 0; j < MAX_SHED_LEVELS; j++) {
            Shed_Levels[i][j] = j + 1;
        }
    }

    return;
}

/* we simply have 0-n object instances.  Yours might be */
/* more complex, and then you need validate that the */
/* given instance exists */
bool Load_Control_Valid_Instance(uint32_t object_instance)
{
    if (object_instance < MAX_LOAD_CONTROLS) {
        return true;
    }

    return false;
}

/* we simply have 0-n object instances.  Yours might be */
/* more complex, and then count how many you have */
unsigned Load_Control_Count(void)
{
    return MAX_LOAD_CONTROLS;
}

/* we simply have 0-n object instances.  Yours might be */
/* more complex, and then you need to return the instance */
/* that correlates to the correct index */
uint32_t Load_Control_Index_To_Instance(unsigned index)
{
    if (index < MAX_LOAD_CONTROLS) {
        return index;
    }
    return MAX_LOAD_CONTROLS;
}

/* we simply have 0-n object instances.  Yours might be */
/* more complex, and then you need to return the index */
/* that correlates to the correct instance number */
unsigned Load_Control_Instance_To_Index(uint32_t object_instance)
{
    unsigned index = MAX_LOAD_CONTROLS;

    if (object_instance < MAX_LOAD_CONTROLS) {
        index = object_instance;
    }

    return index;
}

static BACNET_SHED_STATE Load_Control_Present_Value(uint32_t object_instance)
{
    BACNET_SHED_STATE value = BACNET_SHED_INACTIVE;
    unsigned index = 0;

    index = Load_Control_Instance_To_Index(object_instance);
    if (index < MAX_LOAD_CONTROLS) {
        value = Present_Value[index];
    }

    return value;
}

/* note: the object name must be unique within this device */
bool Load_Control_Object_Name(
    uint32_t object_instance, BACNET_CHARACTER_STRING *object_name)
{
    static char text[32] = ""; /* okay for single thread */
    bool status = false;

    if (object_instance < MAX_LOAD_CONTROLS) {
        snprintf(text, sizeof(text), "LOAD CONTROL %lu", (unsigned long)object_instance);
        status = characterstring_init_ansi(object_name, text);
    }

    return status;
}

static void Update_Current_Time(BACNET_DATE_TIME *bdatetime)
{
    if (bdatetime) {
        datetime_local(&bdatetime->date, &bdatetime->time, NULL, NULL);
    }
}

/* convert the shed level request into an Analog Output Present_Value */
static float Requested_Shed_Level_Value(int object_index)
{
    unsigned shed_level_index = 0;
    unsigned i = 0;
    float requested_level = 0.0;

    switch (Requested_Shed_Level[object_index].type) {
        case BACNET_SHED_TYPE_PERCENT:
            requested_level =
                (float)Requested_Shed_Level[object_index].value.percent;
            break;
        case BACNET_SHED_TYPE_AMOUNT:
            /* Assumptions: wattage is linear with analog output level */
            requested_level = Full_Duty_Baseline[object_index] -
                Requested_Shed_Level[object_index].value.amount;
            requested_level /= Full_Duty_Baseline[object_index];
            requested_level *= 100.0;
            break;
        case BACNET_SHED_TYPE_LEVEL:
        default:
            for (i = 0; i < MAX_SHED_LEVELS; i++) {
                if (Shed_Levels[object_index][i] <=
                    Requested_Shed_Level[object_index].value.level) {
                    shed_level_index = i;
                }
            }
            requested_level = Shed_Level_Values[shed_level_index];
            break;
    }

    return requested_level;
}

static void Shed_Level_Copy(BACNET_SHED_LEVEL *dest, BACNET_SHED_LEVEL *src)
{
    if (dest && src) {
        dest->type = src->type;
        switch (src->type) {
            case BACNET_SHED_TYPE_PERCENT:
                dest->value.percent = src->value.percent;
                break;
            case BACNET_SHED_TYPE_AMOUNT:
                dest->value.amount = src->value.amount;
                break;
            case BACNET_SHED_TYPE_LEVEL:
            default:
                dest->value.level = src->value.level;
                break;
        }
    }
}

static void Shed_Level_Default_Set(
    BACNET_SHED_LEVEL *dest, BACNET_SHED_LEVEL_TYPE type)
{
    if (dest) {
        dest->type = type;
        switch (type) {
            case BACNET_SHED_TYPE_PERCENT:
                dest->value.percent = 100;
                break;
            case BACNET_SHED_TYPE_AMOUNT:
                dest->value.amount = 0.0;
                break;
            case BACNET_SHED_TYPE_LEVEL:
            default:
                dest->value.level = 0;
                break;
        }
    }
}

static bool Able_To_Meet_Shed_Request(int object_index)
{
    float level = 0.0;
    float requested_level = 0.0;
    unsigned priority = 0;
    bool status = false;
    int object_instance = 0;

    /* This demo is going to use the Analog Outputs as their Load */
    object_instance = object_index;
    priority = Analog_Output_Present_Value_Priority(object_instance);
    /* we are controlling at Priority 4 - can we control the output? */
    if (priority >= 4) {
        /* is the level able to be lowered? */
        requested_level = Requested_Shed_Level_Value(object_index);
        level = Analog_Output_Present_Value(object_instance);
        if (level >= requested_level) {
            status = true;
        }
    }

    return status;
}

typedef enum load_control_state {
    SHED_INACTIVE,
    SHED_REQUEST_PENDING,
    SHED_NON_COMPLIANT,
    SHED_COMPLIANT,
    MAX_LOAD_CONTROL_STATE
} LOAD_CONTROL_STATE;
static LOAD_CONTROL_STATE Load_Control_State[MAX_LOAD_CONTROLS];
static LOAD_CONTROL_STATE Load_Control_State_Previously[MAX_LOAD_CONTROLS];

#if PRINT_ENABLED_DEBUG
static void Print_Load_Control_State(int object_index)
{
    char *Load_Control_State_Text[MAX_LOAD_CONTROLS] = { "SHED_INACTIVE",
        "SHED_REQUEST_PENDING", "SHED_NON_COMPLIANT", "SHED_COMPLIANT" };

    if (object_index < MAX_LOAD_CONTROLS) {
        if (Load_Control_State[object_index] < MAX_LOAD_CONTROL_STATE) {
            printf("Load Control[%d]=%s\n", object_index,
                Load_Control_State_Text[Load_Control_State[object_index]]);
        }
    }
}
#endif

void Load_Control_State_Machine(int object_index)
{
    unsigned i = 0; /* loop counter */
    int diff = 0; /* used for datetime comparison */

    /* is the state machine enabled? */
    if (!Load_Control_Enable[object_index]) {
        Load_Control_State[object_index] = SHED_INACTIVE;
        return;
    }

    switch (Load_Control_State[object_index]) {
        case SHED_REQUEST_PENDING:
            if (Load_Control_Request_Written[object_index]) {
                Load_Control_Request_Written[object_index] = false;
                /* request to cancel using default values? */
                switch (Requested_Shed_Level[object_index].type) {
                    case BACNET_SHED_TYPE_PERCENT:
                        if (Requested_Shed_Level[object_index].value.percent ==
                            DEFAULT_VALUE_PERCENT) {
                            Load_Control_State[object_index] = SHED_INACTIVE;
                        }
                        break;
                    case BACNET_SHED_TYPE_AMOUNT:
                        if (Requested_Shed_Level[object_index].value.amount <=
                            DEFAULT_VALUE_AMOUNT) {
                            Load_Control_State[object_index] = SHED_INACTIVE;
                        }
                        break;
                    case BACNET_SHED_TYPE_LEVEL:
                    default:
                        if (Requested_Shed_Level[object_index].value.level ==
                                DEFAULT_VALUE_LEVEL) {
                            Load_Control_State[object_index] = SHED_INACTIVE;
                        }
                        break;
                }
                if (Load_Control_State[object_index] == SHED_INACTIVE) {
#if PRINT_ENABLED_DEBUG
                    printf("Load Control[%d]:Requested Shed Level=Default\n",
                        object_index);
#endif
                    break;
                }
            }
            /* clear the flag for Start time if it is written */
            if (Start_Time_Property_Written[object_index]) {
                Start_Time_Property_Written[object_index] = false;
                /* request to cancel using wildcards in start time? */
                if (datetime_wildcard(&Start_Time[object_index])) {
                    Load_Control_State[object_index] = SHED_INACTIVE;
#if PRINT_ENABLED_DEBUG
                    printf(
                        "Load Control[%d]:Start Time=Wildcard\n", object_index);
#endif
                    break;
                }
            }
            /* cancel because current time is after start time + duration? */
            datetime_copy(&End_Time[object_index], &Start_Time[object_index]);
            datetime_add_minutes(
                &End_Time[object_index], Shed_Duration[object_index]);
            diff = datetime_compare(&End_Time[object_index], &Current_Time);
            if (diff < 0) {
                /* CancelShed */
                /* FIXME: stop shedding! i.e. relinquish */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Current Time"
                       " is after Start Time + Duration\n",
                    object_index);
#endif
                Load_Control_State[object_index] = SHED_INACTIVE;
                break;
            }
            diff = datetime_compare(&Current_Time, &Start_Time[object_index]);
            if (diff < 0) {
                /* current time prior to start time */
                /* ReconfigurePending */
                Shed_Level_Copy(&Expected_Shed_Level[object_index],
                    &Requested_Shed_Level[object_index]);
                Shed_Level_Default_Set(&Actual_Shed_Level[object_index],
                    Requested_Shed_Level[object_index].type);
            } else if (diff > 0) {
                /* current time after to start time */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Current Time"
                       " is after Start Time\n",
                    object_index);
#endif
                /* AbleToMeetShed */
                if (Able_To_Meet_Shed_Request(object_index)) {
                    Shed_Level_Copy(&Expected_Shed_Level[object_index],
                        &Requested_Shed_Level[object_index]);
                    Analog_Output_Present_Value_Set(object_index,
                        Requested_Shed_Level_Value(object_index), 4);
                    Shed_Level_Copy(&Actual_Shed_Level[object_index],
                        &Requested_Shed_Level[object_index]);
                    Load_Control_State[object_index] = SHED_COMPLIANT;
                } else {
                    /* CannotMeetShed */
                    Shed_Level_Default_Set(&Expected_Shed_Level[object_index],
                        Requested_Shed_Level[object_index].type);
                    Shed_Level_Default_Set(&Actual_Shed_Level[object_index],
                        Requested_Shed_Level[object_index].type);
                    Load_Control_State[object_index] = SHED_NON_COMPLIANT;
                }
            }
            break;
        case SHED_NON_COMPLIANT:
            datetime_copy(&End_Time[object_index], &Start_Time[object_index]);
            datetime_add_minutes(
                &End_Time[object_index], Shed_Duration[object_index]);
            diff = datetime_compare(&End_Time[object_index], &Current_Time);
            if (diff < 0) {
                /* FinishedUnsuccessfulShed */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Current Time is after Start Time + "
                       "Duration\n",
                    object_index);
#endif
                Load_Control_State[object_index] = SHED_INACTIVE;
                break;
            }
            if (Load_Control_Request_Written[object_index] ||
                Start_Time_Property_Written[object_index]) {
                /* UnsuccessfulShedReconfigured */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Control Property written\n",
                    object_index);
#endif
                /* The Written flags will cleared in the next state */
                Load_Control_State[object_index] = SHED_REQUEST_PENDING;
                break;
            }
            if (Able_To_Meet_Shed_Request(object_index)) {
                /* CanNowComplyWithShed */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Able to meet Shed Request\n",
                    object_index);
#endif
                Shed_Level_Copy(&Expected_Shed_Level[object_index],
                    &Requested_Shed_Level[object_index]);
                Analog_Output_Present_Value_Set(
                    object_index, Requested_Shed_Level_Value(object_index), 4);
                Shed_Level_Copy(&Actual_Shed_Level[object_index],
                    &Requested_Shed_Level[object_index]);
                Load_Control_State[object_index] = SHED_COMPLIANT;
            }
            break;
        case SHED_COMPLIANT:
            datetime_copy(&End_Time[object_index], &Start_Time[object_index]);
            datetime_add_minutes(
                &End_Time[object_index], Shed_Duration[object_index]);
            diff = datetime_compare(&End_Time[object_index], &Current_Time);
            if (diff < 0) {
                /* FinishedSuccessfulShed */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Current Time is after Start Time + "
                       "Duration\n",
                    object_index);
#endif
                datetime_wildcard_set(&Start_Time[i]);
                Analog_Output_Present_Value_Relinquish(object_index, 4);
                Load_Control_State[object_index] = SHED_INACTIVE;
                break;
            }
            if (Load_Control_Request_Written[object_index] ||
                Start_Time_Property_Written[object_index]) {
                /* UnsuccessfulShedReconfigured */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Control Property written\n",
                    object_index);
#endif
                /* The Written flags will cleared in the next state */
                Load_Control_State[object_index] = SHED_REQUEST_PENDING;
                break;
            }
            if (!Able_To_Meet_Shed_Request(object_index)) {
                /* CanNoLongerComplyWithShed */
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Not able to meet Shed Request\n",
                    object_index);
#endif
                Shed_Level_Default_Set(&Expected_Shed_Level[object_index],
                    Requested_Shed_Level[object_index].type);
                Shed_Level_Default_Set(&Actual_Shed_Level[object_index],
                    Requested_Shed_Level[object_index].type);
                Load_Control_State[object_index] = SHED_NON_COMPLIANT;
            }
            break;
        case SHED_INACTIVE:
        default:
            if (Start_Time_Property_Written[object_index]) {
#if PRINT_ENABLED_DEBUG
                printf("Load Control[%d]:Start Time written\n", object_index);
#endif
                /* The Written flag will cleared in the next state */
                Shed_Level_Copy(&Expected_Shed_Level[object_index],
                    &Requested_Shed_Level[object_index]);
                Shed_Level_Default_Set(&Actual_Shed_Level[object_index],
                    Requested_Shed_Level[object_index].type);
                Load_Control_State[object_index] = SHED_REQUEST_PENDING;
            }
            break;
    }

    return;
}

/* call every second or so */
void Load_Control_State_Machine_Handler(void)
{
    unsigned i = 0;
    static bool initialized = false;

    if (!initialized) {
        initialized = true;
        for (i = 0; i < MAX_LOAD_CONTROLS; i++) {
            Load_Control_State[i] = SHED_INACTIVE;
            Load_Control_State_Previously[i] = SHED_INACTIVE;
        }
    }
    Update_Current_Time(&Current_Time);
    for (i = 0; i < MAX_LOAD_CONTROLS; i++) {
        Load_Control_State_Machine(i);
        if (Load_Control_State[i] != Load_Control_State_Previously[i]) {
#if PRINT_ENABLED_DEBUG
            Print_Load_Control_State(i);
#endif
            Load_Control_State_Previously[i] = Load_Control_State[i];
        }
    }
}

/* return apdu len, or BACNET_STATUS_ERROR on error */
int Load_Control_Read_Property(BACNET_READ_PROPERTY_DATA *rpdata)
{
    int len = 0;
    int apdu_len = 0; /* return value */
    BACNET_BIT_STRING bit_string;
    BACNET_CHARACTER_STRING char_string;
    int enumeration = 0;
    unsigned object_index = 0;
    unsigned i = 0;
    bool state = false;
    uint8_t *apdu = NULL;

    if ((rpdata == NULL) || (rpdata->application_data == NULL) ||
        (rpdata->application_data_len == 0)) {
        return 0;
    }
    apdu = rpdata->application_data;
    object_index = Load_Control_Instance_To_Index(rpdata->object_instance);
    switch (rpdata->object_property) {
        case PROP_OBJECT_IDENTIFIER:
            apdu_len = encode_application_object_id(
                &apdu[0], OBJECT_LOAD_CONTROL, rpdata->object_instance);
            break;
        case PROP_OBJECT_NAME:
        case PROP_DESCRIPTION:
            Load_Control_Object_Name(rpdata->object_instance, &char_string);
            apdu_len =
                encode_application_character_string(&apdu[0], &char_string);
            break;
        case PROP_OBJECT_TYPE:
            apdu_len =
                encode_application_enumerated(&apdu[0], OBJECT_LOAD_CONTROL);
            break;
        case PROP_PRESENT_VALUE:
            enumeration = Load_Control_Present_Value(rpdata->object_instance);
            apdu_len = encode_application_enumerated(&apdu[0], enumeration);
            break;
        case PROP_STATUS_FLAGS:
            bitstring_init(&bit_string);
            /* IN_ALARM - Logical FALSE (0) if the Event_State property
               has a value of NORMAL, otherwise logical TRUE (1). */
            bitstring_set_bit(&bit_string, STATUS_FLAG_IN_ALARM, false);
            /* FAULT - Logical      TRUE (1) if the Reliability property is
               present and does not have a value of NO_FAULT_DETECTED,
               otherwise logical FALSE (0). */
            bitstring_set_bit(&bit_string, STATUS_FLAG_FAULT, false);
            /* OVERRIDDEN - Logical TRUE (1) if the point has been
               overridden by some mechanism local to the BACnet Device,
               otherwise logical FALSE (0). */
            bitstring_set_bit(&bit_string, STATUS_FLAG_OVERRIDDEN, false);
            /* OUT_OF_SERVICE - This bit shall always be Logical FALSE (0). */
            bitstring_set_bit(&bit_string, STATUS_FLAG_OUT_OF_SERVICE, false);
            apdu_len = encode_application_bitstring(&apdu[0], &bit_string);
            break;
        case PROP_EVENT_STATE:
            apdu_len =
                encode_application_enumerated(&apdu[0], EVENT_STATE_NORMAL);
            break;
        case PROP_REQUESTED_SHED_LEVEL:
            switch (Requested_Shed_Level[object_index].type) {
                case BACNET_SHED_TYPE_PERCENT:
                    apdu_len = encode_context_unsigned(&apdu[0], 0,
                        Requested_Shed_Level[object_index].value.percent);
                    break;
                case BACNET_SHED_TYPE_AMOUNT:
                    apdu_len = encode_context_real(&apdu[0], 2,
                        Requested_Shed_Level[object_index].value.amount);
                    break;
                case BACNET_SHED_TYPE_LEVEL:
                default:
                    apdu_len = encode_context_unsigned(&apdu[0], 1,
                        Requested_Shed_Level[object_index].value.level);
                    break;
            }
            break;
        case PROP_START_TIME:
            len = encode_application_date(
                &apdu[0], &Start_Time[object_index].date);
            apdu_len = len;
            len = encode_application_time(
                &apdu[apdu_len], &Start_Time[object_index].time);
            apdu_len += len;
            break;
        case PROP_SHED_DURATION:
            apdu_len = encode_application_unsigned(
                &apdu[0], Shed_Duration[object_index]);
            break;
        case PROP_DUTY_WINDOW:
            apdu_len = encode_application_unsigned(
                &apdu[0], Duty_Window[object_index]);
            break;
        case PROP_ENABLE:
            state = Load_Control_Enable[object_index];
            apdu_len = encode_application_boolean(&apdu[0], state);
            break;
        case PROP_FULL_DUTY_BASELINE: /* optional */
            apdu_len = encode_application_real(
                &apdu[0], Full_Duty_Baseline[object_index]);
            break;
        case PROP_EXPECTED_SHED_LEVEL:
            switch (Expected_Shed_Level[object_index].type) {
                case BACNET_SHED_TYPE_PERCENT:
                    apdu_len = encode_context_unsigned(&apdu[0], 0,
                        Expected_Shed_Level[object_index].value.percent);
                    break;
                case BACNET_SHED_TYPE_AMOUNT:
                    apdu_len = encode_context_real(&apdu[0], 2,
                        Expected_Shed_Level[object_index].value.amount);
                    break;
                case BACNET_SHED_TYPE_LEVEL:
                default:
                    apdu_len = encode_context_unsigned(&apdu[0], 1,
                        Expected_Shed_Level[object_index].value.level);
                    break;
            }
            break;
        case PROP_ACTUAL_SHED_LEVEL:
            switch (Actual_Shed_Level[object_index].type) {
                case BACNET_SHED_TYPE_PERCENT:
                    apdu_len = encode_context_unsigned(&apdu[0], 0,
                        Actual_Shed_Level[object_index].value.percent);
                    break;
                case BACNET_SHED_TYPE_AMOUNT:
                    apdu_len = encode_context_real(&apdu[0], 2,
                        Actual_Shed_Level[object_index].value.amount);
                    break;
                case BACNET_SHED_TYPE_LEVEL:
                default:
                    apdu_len = encode_context_unsigned(&apdu[0], 1,
                        Actual_Shed_Level[object_index].value.level);
                    break;
            }
            break;
        case PROP_SHED_LEVELS:
            /* Array element zero is the number of elements in the array */
            if (rpdata->array_index == 0) {
                apdu_len =
                    encode_application_unsigned(&apdu[0], MAX_SHED_LEVELS);
                /* if no index was specified, then try to encode the entire list
                 */
                /* into one packet. */
            } else if (rpdata->array_index == BACNET_ARRAY_ALL) {
                apdu_len = 0;
                for (i = 0; i < MAX_SHED_LEVELS; i++) {
                    /* FIXME: check if we have room before adding it to APDU */
                    len = encode_application_unsigned(
                        &apdu[apdu_len], Shed_Levels[object_index][i]);
                    /* add it if we have room */
                    if ((apdu_len + len) < MAX_APDU) {
                        apdu_len += len;
                    } else {
                        rpdata->error_code =
                            ERROR_CODE_ABORT_SEGMENTATION_NOT_SUPPORTED;
                        apdu_len = BACNET_STATUS_ABORT;
                        break;
                    }
                }
            } else {
                if (rpdata->array_index <= MAX_SHED_LEVELS) {
                    apdu_len = encode_application_unsigned(&apdu[0],
                        Shed_Levels[object_index][rpdata->array_index - 1]);
                } else {
                    rpdata->error_class = ERROR_CLASS_PROPERTY;
                    rpdata->error_code = ERROR_CODE_INVALID_ARRAY_INDEX;
                    apdu_len = BACNET_STATUS_ERROR;
                }
            }
            break;
        case PROP_SHED_LEVEL_DESCRIPTIONS:
            /* Array element zero is the number of elements in the array */
            if (rpdata->array_index == 0) {
                apdu_len =
                    encode_application_unsigned(&apdu[0], MAX_SHED_LEVELS);
                /* if no index was specified, then try to encode the entire list
                 */
                /* into one packet. */
            } else if (rpdata->array_index == BACNET_ARRAY_ALL) {
                apdu_len = 0;
                for (i = 0; i < MAX_SHED_LEVELS; i++) {
                    /* FIXME: check if we have room before adding it to APDU */
                    characterstring_init_ansi(
                        &char_string, Shed_Level_Descriptions[i]);
                    len = encode_application_character_string(
                        &apdu[apdu_len], &char_string);
                    /* add it if we have room */
                    if ((apdu_len + len) < MAX_APDU) {
                        apdu_len += len;
                    } else {
                        rpdata->error_code =
                            ERROR_CODE_ABORT_SEGMENTATION_NOT_SUPPORTED;
                        apdu_len = BACNET_STATUS_ABORT;
                        break;
                    }
                }
            } else {
                if (rpdata->array_index <= MAX_SHED_LEVELS) {
                    characterstring_init_ansi(&char_string,
                        Shed_Level_Descriptions[rpdata->array_index - 1]);
                    apdu_len = encode_application_character_string(
                        &apdu[0], &char_string);
                } else {
                    rpdata->error_class = ERROR_CLASS_PROPERTY;
                    rpdata->error_code = ERROR_CODE_INVALID_ARRAY_INDEX;
                    apdu_len = BACNET_STATUS_ERROR;
                }
            }
            break;
        default:
            rpdata->error_class = ERROR_CLASS_PROPERTY;
            rpdata->error_code = ERROR_CODE_UNKNOWN_PROPERTY;
            apdu_len = BACNET_STATUS_ERROR;
            break;
    }
    /*  only array properties can have array options */
    if ((apdu_len >= 0) &&
        (rpdata->object_property != PROP_SHED_LEVEL_DESCRIPTIONS) &&
        (rpdata->object_property != PROP_SHED_LEVELS) &&
        (rpdata->array_index != BACNET_ARRAY_ALL)) {
        rpdata->error_class = ERROR_CLASS_PROPERTY;
        rpdata->error_code = ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY;
        apdu_len = BACNET_STATUS_ERROR;
    }

    return apdu_len;
}

/* returns true if successful */
bool Load_Control_Write_Property(BACNET_WRITE_PROPERTY_DATA *wp_data)
{
    bool status = false; /* return value */
    unsigned int object_index = 0;
    int len = 0;
    BACNET_APPLICATION_DATA_VALUE value;
    /* build here in case of error in time half of datetime */
    BACNET_DATE start_date;

    PRINTF("Load_Control_Write_Property(wp_data=%p)\n", wp_data);
    if (wp_data == NULL) {
        PRINTF("Load_Control_Write_Property() failure detected point A\n");
        return false;
    }
    if (wp_data->application_data_len < 0) {
        PRINTF("Load_Control_Write_Property() failure detected point A.2\n");
        /* error while decoding - a smaller larger than we can handle */
        wp_data->error_class = ERROR_CLASS_PROPERTY;
        wp_data->error_code = ERROR_CODE_VALUE_OUT_OF_RANGE;
        return false;
    }

    /* decode the some of the request */
    len = bacapp_decode_application_data(
        wp_data->application_data, wp_data->application_data_len, &value);
    /* FIXME: len < application_data_len: more data? */
    if (len < 0) {
        PRINTF("Load_Control_Write_Property() failure detected point B\n");
        /* error while decoding - a value larger than we can handle */
        wp_data->error_class = ERROR_CLASS_PROPERTY;
        wp_data->error_code = ERROR_CODE_VALUE_OUT_OF_RANGE;
        return false;
    }
    /*  only array properties can have array options */
    if ((wp_data->object_property != PROP_SHED_LEVELS) &&
        (wp_data->array_index != BACNET_ARRAY_ALL)) {
        PRINTF("Load_Control_Write_Property() failure detected point C\n");
        wp_data->error_class = ERROR_CLASS_PROPERTY;
        wp_data->error_code = ERROR_CODE_PROPERTY_IS_NOT_AN_ARRAY;
        return false;
    }
    object_index = Load_Control_Instance_To_Index(wp_data->object_instance);
    switch (wp_data->object_property) {
        case PROP_REQUESTED_SHED_LEVEL:
            len = bacapp_decode_context_data(wp_data->application_data,
                wp_data->application_data_len, &value,
                PROP_REQUESTED_SHED_LEVEL);
            if (len == BACNET_STATUS_ERROR) {
                PRINTF(
                    "Load_Control_Write_Property() failure detected point D\n");
                /* error! */
                wp_data->error_class = ERROR_CLASS_PROPERTY;
                wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
            } else if (value.context_tag == 0) {
                /* percent - Unsigned */
                Requested_Shed_Level[object_index].type =
                    BACNET_SHED_TYPE_PERCENT;
                Requested_Shed_Level[object_index].value.percent =
                    value.type.Unsigned_Int;
                status = true;
            } else if (value.context_tag == 1) {
                /* level - Unsigned */
                Requested_Shed_Level[object_index].type =
                    BACNET_SHED_TYPE_LEVEL;
                Requested_Shed_Level[object_index].value.level =
                    value.type.Unsigned_Int;
                status = true;
            } else if (value.context_tag == 2) {
                /* amount - REAL */
                Requested_Shed_Level[object_index].type =
                    BACNET_SHED_TYPE_AMOUNT;
                Requested_Shed_Level[object_index].value.amount =
                    value.type.Real;
                status = true;
            } else {
                PRINTF(
                    "Load_Control_Write_Property() failure detected point E\n");
                /* error! */
                wp_data->error_class = ERROR_CLASS_PROPERTY;
                wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
            }
            if (status) {
                Load_Control_Request_Written[object_index] = true;
            }
            break;

        case PROP_START_TIME:
            status = write_property_type_valid(
                wp_data, &value, BACNET_APPLICATION_TAG_DATE);
            if (!status) {
                PRINTF(
                    "Load_Control_Write_Property() failure detected point F\n");
                /* don't continue if we don't have a valid date */
                break;
            }
            /* Hold the date until we are sure the time is also there */
            start_date = value.type.Date;
            len =
                bacapp_decode_application_data(wp_data->application_data + len,
                    wp_data->application_data_len - len, &value);
            if (len) {
                status = write_property_type_valid(
                    wp_data, &value, BACNET_APPLICATION_TAG_TIME);
                if (status) {
                    /* Write time and date and set written flag */
                    Start_Time[object_index].date = start_date;
                    Start_Time[object_index].time = value.type.Time;
                    Start_Time_Property_Written[object_index] = true;
                }
            } else {
                PRINTF(
                    "Load_Control_Write_Property() failure detected point G\n");
                status = false;
                wp_data->error_class = ERROR_CLASS_PROPERTY;
                wp_data->error_code = ERROR_CODE_VALUE_OUT_OF_RANGE;
            }
            break;

        case PROP_SHED_DURATION:
            status = write_property_type_valid(
                wp_data, &value, BACNET_APPLICATION_TAG_UNSIGNED_INT);
            if (status) {
                Shed_Duration[object_index] = value.type.Unsigned_Int;
                Load_Control_Request_Written[object_index] = true;
            } else {
                PRINTF(
                    "Load_Control_Write_Property() failure detected point H\n");
            }
            break;

        case PROP_DUTY_WINDOW:
            status = write_property_type_valid(
                wp_data, &value, BACNET_APPLICATION_TAG_UNSIGNED_INT);
            if (status) {
                Duty_Window[object_index] = value.type.Unsigned_Int;
                Load_Control_Request_Written[object_index] = true;
            }
            break;

        case PROP_SHED_LEVELS:
            status = write_property_type_valid(
                wp_data, &value, BACNET_APPLICATION_TAG_UNSIGNED_INT);
            if (status) {
                /* re-write the size of the array? */
                if (wp_data->array_index == 0) {
                    wp_data->error_class = ERROR_CLASS_PROPERTY;
                    wp_data->error_code = ERROR_CODE_WRITE_ACCESS_DENIED;
                    status = false;
                } else if (wp_data->array_index == BACNET_ARRAY_ALL) {
                    /* FIXME: write entire array */
                } else if (wp_data->array_index <= MAX_SHED_LEVELS) {
                    Shed_Levels[object_index][wp_data->array_index - 1] =
                        value.type.Unsigned_Int;
                } else {
                    /* FIXME: Something's missing from here so I'll just put in
                     * a place holder error here for the moment*/
                    wp_data->error_class = ERROR_CLASS_PROPERTY;
                    wp_data->error_code = ERROR_CODE_OTHER;
                    status = false;
                }
            }
            break;

        case PROP_ENABLE:
            status = write_property_type_valid(
                wp_data, &value, BACNET_APPLICATION_TAG_BOOLEAN);
            if (status) {
                Load_Control_Enable[object_index] = value.type.Boolean;
            }
            break;
        default:
            PRINTF("Load_Control_Write_Property() failure detected point Z\n");
            wp_data->error_class = ERROR_CLASS_PROPERTY;
            wp_data->error_code = ERROR_CODE_WRITE_ACCESS_DENIED;
            break;
    }

    PRINTF("Load_Control_Write_Property() returning status=%d\n", status);
    return status;
}
