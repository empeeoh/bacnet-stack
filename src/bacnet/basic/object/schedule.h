/**
 * @file
 * @author Nikola Jelic <nikola.jelic@euroicc.com>
 * @date 2015
 * @brief API for a basic BACnet Schedule object implementation.
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef BACNET_BASIC_OBJECT_SCHEDULE_H
#define BACNET_BASIC_OBJECT_SCHEDULE_H
#include <stdbool.h>
#include <stdint.h>
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bacapp.h"
#include "bacnet/datetime.h"
#include "bacnet/bacerror.h"
#include "bacnet/wp.h"
#include "bacnet/rp.h"
#include "bacnet/bacdevobjpropref.h"
#include "bacnet/bactimevalue.h"

#ifndef BACNET_WEEKLY_SCHEDULE_SIZE
#define BACNET_WEEKLY_SCHEDULE_SIZE 8   /* maximum number of data points for each day */
#endif

#ifndef BACNET_SCHEDULE_OBJ_PROP_REF_SIZE
#define BACNET_SCHEDULE_OBJ_PROP_REF_SIZE 4     /* maximum number of obj prop references */
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

    /*
     * Note:
     * This is a different struct from BACNET_DAILY_SCHEDULE used in prop value encoding!
     * The number of entries is different.
     */
    typedef struct bacnet_obj_daily_schedule {
        BACNET_TIME_VALUE Time_Values[BACNET_WEEKLY_SCHEDULE_SIZE];
        uint16_t TV_Count;      /* the number of time values actually used */
    } BACNET_OBJ_DAILY_SCHEDULE;

    typedef struct schedule {
        /* Effective Period: Start and End Date */
        BACNET_DATE Start_Date;
        BACNET_DATE End_Date;
        /* Properties concerning Present Value */
        BACNET_OBJ_DAILY_SCHEDULE Weekly_Schedule[7];
        BACNET_APPLICATION_DATA_VALUE Schedule_Default;
        /*
         * Caution: This is a converted to BACNET_PRIMITIVE_APPLICATION_DATA_VALUE.
         * Only some data types may be used!
         */
        BACNET_APPLICATION_DATA_VALUE Present_Value;   /* must be set to a valid value
                                                         * default is Schedule_Default */
        BACNET_DEVICE_OBJECT_PROPERTY_REFERENCE
            Object_Property_References[BACNET_SCHEDULE_OBJ_PROP_REF_SIZE];
        uint8_t obj_prop_ref_cnt;       /* actual number of obj_prop references */
        uint8_t Priority_For_Writing;   /* (1..16) */
        bool Out_Of_Service;
    } SCHEDULE_DESCR;

    BACNET_STACK_EXPORT
    void Schedule_Property_Lists(const int **pRequired,
        const int **pOptional,
        const int **pProprietary);

    BACNET_STACK_EXPORT
    bool Schedule_Valid_Instance(uint32_t object_instance);
    BACNET_STACK_EXPORT
    unsigned Schedule_Count(void);
    BACNET_STACK_EXPORT
    uint32_t Schedule_Index_To_Instance(unsigned index);
    BACNET_STACK_EXPORT
    unsigned Schedule_Instance_To_Index(uint32_t instance);
    BACNET_STACK_EXPORT
    void Schedule_Init(void);

    BACNET_STACK_EXPORT
    void Schedule_Out_Of_Service_Set(
        uint32_t object_instance,
        bool value);
    BACNET_STACK_EXPORT
    bool Schedule_Out_Of_Service(
        uint32_t object_instance);


    BACNET_STACK_EXPORT
    bool Schedule_Object_Name(uint32_t object_instance,
        BACNET_CHARACTER_STRING * object_name);

    BACNET_STACK_EXPORT
    int Schedule_Read_Property(BACNET_READ_PROPERTY_DATA * rpdata);
    BACNET_STACK_EXPORT
    bool Schedule_Write_Property(BACNET_WRITE_PROPERTY_DATA * wp_data);

    /* utility functions for calculating current Present Value
     * if Exception Schedule is to be added, these functions must take that into account */
    BACNET_STACK_EXPORT
    bool Schedule_In_Effective_Period(SCHEDULE_DESCR * desc,
        BACNET_DATE * date);
    BACNET_STACK_EXPORT
    void Schedule_Recalculate_PV(SCHEDULE_DESCR * desc,
        BACNET_WEEKDAY wday,
        BACNET_TIME * time);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif
