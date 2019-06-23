/*

Copyright 2015-2019 Igor Petrovic

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

#include "Pins.h"
#include "board/common/Map.h"

///
/// brief Pin and index remapping used to match indexes on PCB silkscreen.
/// @{

#if HARDWARE_VERSION_MINOR == 1
#define MUX_Y0 8
#define MUX_Y1 9
#define MUX_Y2 10
#define MUX_Y3 11
#define MUX_Y4 12
#define MUX_Y5 13
#define MUX_Y6 14
#define MUX_Y7 15
#define MUX_Y8 7
#define MUX_Y9 6
#define MUX_Y10 5
#define MUX_Y11 4
#define MUX_Y12 3
#define MUX_Y13 2
#define MUX_Y14 1
#define MUX_Y15 0
#elif HARDWARE_VERSION_MINOR == 2
#define MUX_Y0 8
#define MUX_Y1 9
#define MUX_Y2 10
#define MUX_Y3 11
#define MUX_Y4 12
#define MUX_Y5 13
#define MUX_Y6 14
#define MUX_Y7 15
#define MUX_Y8 4
#define MUX_Y9 5
#define MUX_Y10 7
#define MUX_Y11 6
#define MUX_Y12 0
#define MUX_Y13 1
#define MUX_Y14 2
#define MUX_Y15 3
#endif

#define DM_ROW_1_BIT 0
#define DM_ROW_2_BIT 1
#define DM_ROW_3_BIT 2
#define DM_ROW_4_BIT 3
#define DM_ROW_5_BIT 7
#define DM_ROW_6_BIT 6
#define DM_ROW_7_BIT 5
#define DM_ROW_8_BIT 4

#define DM_COLUMN_1 0
#define DM_COLUMN_2 7
#define DM_COLUMN_3 2
#define DM_COLUMN_4 1
#define DM_COLUMN_5 4
#define DM_COLUMN_6 3
#define DM_COLUMN_7 5
#define DM_COLUMN_8 6

namespace Board
{
    namespace map
    {
        namespace
        {
            const uint8_t muxPinOrderArray[NUMBER_OF_MUX_INPUTS] = {
                MUX_Y0,
                MUX_Y1,
                MUX_Y2,
                MUX_Y3,
                MUX_Y4,
                MUX_Y5,
                MUX_Y6,
                MUX_Y7,
                MUX_Y8,
                MUX_Y9,
                MUX_Y10,
                MUX_Y11,
                MUX_Y12,
                MUX_Y13,
                MUX_Y14,
                MUX_Y15
            };

            //row bits are stored in inverse order when performing read
            const uint8_t dmRowBitArray[NUMBER_OF_BUTTON_ROWS] = {
                DM_ROW_8_BIT,
                DM_ROW_7_BIT,
                DM_ROW_6_BIT,
                DM_ROW_5_BIT,
                DM_ROW_4_BIT,
                DM_ROW_3_BIT,
                DM_ROW_2_BIT,
                DM_ROW_1_BIT
            };

            const uint8_t dmColumnArray[NUMBER_OF_BUTTON_COLUMNS] = {
                DM_COLUMN_1,
                DM_COLUMN_2,
                DM_COLUMN_3,
                DM_COLUMN_4,
                DM_COLUMN_5,
                DM_COLUMN_6,
                DM_COLUMN_7,
                DM_COLUMN_8
            };

            ///
            /// \brief Array holding ADC read pins/channels.
            ///
            const uint8_t adcChannelArray[NUMBER_OF_MUX] = {
                MUX_1_IN_PIN,
                MUX_2_IN_PIN
            };

            ///
            /// \brief Array of mcuPin_t structure holding port/pin for every LED row for easier access.
            ///
            const core::CORE_ARCH::pins::mcuPin_t ledRowPins[NUMBER_OF_LED_ROWS] = {
                {
                    .port = &LED_ROW_1_PORT,
                    .pin = LED_ROW_1_PIN,
                },

                {
                    .port = &LED_ROW_2_PORT,
                    .pin = LED_ROW_2_PIN,
                },

                {
                    .port = &LED_ROW_3_PORT,
                    .pin = LED_ROW_3_PIN,
                },

                {
                    .port = &LED_ROW_4_PORT,
                    .pin = LED_ROW_4_PIN,
                },

                {
                    .port = &LED_ROW_5_PORT,
                    .pin = LED_ROW_5_PIN,
                },

                {
                    .port = &LED_ROW_6_PORT,
                    .pin = LED_ROW_6_PIN,
                }
            };

            Board::mcuPin_t pin;
        }    // namespace

        uint8_t adcChannel(uint8_t index)
        {
            return adcChannelArray[index];
        }

        uint8_t muxChannel(uint8_t index)
        {
            return muxPinOrderArray[index];
        }

        uint8_t inMatrixRow(uint8_t index)
        {
            return dmRowBitArray[index];
        }

        uint8_t inMatrixColumn(uint8_t index)
        {
            return dmColumnArray[index];
        }

        core::CORE_ARCH::pins::mcuPin_t led(uint8_t index)
        {
            return ledRowPins[index];
        }

        void ledRowOn(uint8_t row, uint8_t intensity)
        {
            if (intensity == 255)
            {
                pin = Board::map::led(row);

                //max value, don't use pwm
                EXT_LED_ON(*pin.port, pin.pin);
            }
            else
            {
#ifdef LED_EXT_INVERT
                intensity = 255 - intensity;
#endif

                switch (row)
                {
                case 0:
                    OCR1C = intensity;
                    TCCR1A |= (1 << COM1C1);
                    break;

                case 1:
                    OCR4D = intensity;
                    TCCR4C |= (1 << COM4D1);
                    break;

                case 2:
                    OCR1A = intensity;
                    TCCR1A |= (1 << COM1A1);
                    break;

                case 3:
                    OCR4A = intensity;
                    TCCR4A |= (1 << COM4A1);
                    break;

                case 4:
                    OCR3A = intensity;
                    TCCR3A |= (1 << COM3A1);
                    break;

                case 5:
                    OCR1B = intensity;
                    TCCR1A |= (1 << COM1B1);
                    break;

                default:
                    break;
                }
            }
        }

        void ledRowOff(uint8_t row)
        {
            //turn off pwm first
            switch (row)
            {
            case 0:
                TCCR1A &= ~(1 << COM1C1);
                break;

            case 1:
                TCCR4C &= ~(1 << COM4D1);
                break;

            case 2:
                TCCR1A &= ~(1 << COM1A1);
                break;

            case 3:
                TCCR4A &= ~(1 << COM4A1);
                break;

            case 4:
                TCCR3A &= ~(1 << COM3A1);
                break;

            case 5:
                TCCR1A &= ~(1 << COM1B1);
                break;

            default:
                return;
                break;
            }

            pin = Board::map::led(row);
            EXT_LED_OFF(*pin.port, pin.pin);
        }
    }    // namespace map
}    // namespace Board

/// @}
