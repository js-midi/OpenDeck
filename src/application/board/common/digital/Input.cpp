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

#include "board/Board.h"
#include "board/common/constants/DigitalIn.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "board/common/digital/Input.h"
#include "board/common/Map.h"
#include "core/src/general/BitManipulation.h"
#include "Pins.h"

namespace
{
    volatile uint8_t digitalInBuffer[DIGITAL_IN_BUFFER_SIZE][DIGITAL_IN_ARRAY_SIZE];
    uint8_t          digitalInBufferReadOnly[DIGITAL_IN_ARRAY_SIZE];

#ifdef NUMBER_OF_BUTTON_COLUMNS
    volatile uint8_t activeInColumn;
#endif

    volatile uint8_t dIn_head;
    volatile uint8_t dIn_tail;
    volatile uint8_t dIn_count;
}    // namespace

namespace Board
{
    namespace interface
    {
        namespace digital
        {
            namespace input
            {
                bool getButtonState(uint8_t buttonID)
                {
#ifdef NUMBER_OF_BUTTON_COLUMNS
                    uint8_t row = buttonID / NUMBER_OF_BUTTON_COLUMNS;
                    uint8_t column = buttonID % NUMBER_OF_BUTTON_COLUMNS;

                    return BIT_READ(digitalInBufferReadOnly[column], row);
#else
                    uint8_t arrayIndex = buttonID / 8;
                    uint8_t buttonIndex = buttonID - 8 * arrayIndex;

                    return BIT_READ(digitalInBufferReadOnly[arrayIndex], buttonIndex);
#endif
                }

                uint8_t getEncoderPair(uint8_t buttonID)
                {
#ifdef NUMBER_OF_BUTTON_COLUMNS
                    uint8_t row = buttonID / NUMBER_OF_BUTTON_COLUMNS;
                    uint8_t column = buttonID % NUMBER_OF_BUTTON_COLUMNS;

                    if (row % 2)
                        row -= 1;    //uneven row, get info from previous (even) row

                    return (row * NUMBER_OF_BUTTON_COLUMNS) / 2 + column;
#else
                    return buttonID / 2;
#endif
                }

                uint8_t getEncoderPairState(uint8_t encoderID)
                {
#ifdef NUMBER_OF_BUTTON_COLUMNS
                    uint8_t column = encoderID % NUMBER_OF_BUTTON_COLUMNS;
                    uint8_t row = (encoderID / NUMBER_OF_BUTTON_COLUMNS) * 2;
                    uint8_t pairState = (digitalInBufferReadOnly[column] >> row) & 0x03;
#else
                    uint8_t buttonID = encoderID * 2;

                    uint8_t pairState = getButtonState(buttonID);
                    pairState <<= 1;
                    pairState |= getButtonState(buttonID + 1);
#endif

                    return pairState;
                }

                bool isDataAvailable()
                {
                    if (dIn_count)
                    {
#ifdef __AVR__
                        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#endif
                        {
                            if (++dIn_tail == DIGITAL_IN_BUFFER_SIZE)
                                dIn_tail = 0;

                            for (int i = 0; i < DIGITAL_IN_ARRAY_SIZE; i++)
                                digitalInBufferReadOnly[i] = digitalInBuffer[dIn_tail][i];

                            dIn_count--;
                        }

                        return true;
                    }

                    return false;
                }

                namespace detail
                {
#if defined(SR_DIN_CLK_PORT) && defined(SR_DIN_LATCH_PORT) && defined(SR_DIN_DATA_PORT) && !defined(NUMBER_OF_BUTTON_COLUMNS) && !defined(NUMBER_OF_BUTTON_ROWS)
                    inline void storeDigitalIn()
                    {
                        CORE_AVR_PIN_SET_LOW(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                        CORE_AVR_PIN_SET_LOW(SR_DIN_LATCH_PORT, SR_DIN_LATCH_PIN);
                        _NOP();

                        CORE_AVR_PIN_SET_HIGH(SR_DIN_LATCH_PORT, SR_DIN_LATCH_PIN);

                        for (int j = 0; j < NUMBER_OF_IN_SR; j++)
                        {
                            for (int i = 0; i < NUMBER_OF_IN_SR_INPUTS; i++)
                            {
                                CORE_AVR_PIN_SET_LOW(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                                _NOP();
                                BIT_WRITE(digitalInBuffer[dIn_head][0], 7 - i, !CORE_AVR_PIN_READ(SR_DIN_DATA_PORT, SR_DIN_DATA_PIN));
                                CORE_AVR_PIN_SET_HIGH(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                            }
                        }
                    }
#elif defined(NUMBER_OF_BUTTON_COLUMNS) && defined(NUMBER_OF_BUTTON_ROWS)
                    inline void activateInputColumn()
                    {
                        BIT_READ(Board::map::inMatrixColumn(activeInColumn), 0) ? CORE_AVR_PIN_SET_HIGH(DEC_DM_A0_PORT, DEC_DM_A0_PIN) : CORE_AVR_PIN_SET_LOW(DEC_DM_A0_PORT, DEC_DM_A0_PIN);
                        BIT_READ(Board::map::inMatrixColumn(activeInColumn), 1) ? CORE_AVR_PIN_SET_HIGH(DEC_DM_A1_PORT, DEC_DM_A1_PIN) : CORE_AVR_PIN_SET_LOW(DEC_DM_A1_PORT, DEC_DM_A1_PIN);
                        BIT_READ(Board::map::inMatrixColumn(activeInColumn), 2) ? CORE_AVR_PIN_SET_HIGH(DEC_DM_A2_PORT, DEC_DM_A2_PIN) : CORE_AVR_PIN_SET_LOW(DEC_DM_A2_PORT, DEC_DM_A2_PIN);

                        if (++activeInColumn == NUMBER_OF_BUTTON_COLUMNS)
                            activeInColumn = 0;
                    }

                    ///
                    /// Acquires data for all buttons connected in currently active button matrix column by
                    /// reading inputs from shift register.
                    ///
                    inline void storeDigitalIn()
                    {
                        for (int i = 0; i < NUMBER_OF_BUTTON_COLUMNS; i++)
                        {
                            activateInputColumn();
                            _NOP();

                            CORE_AVR_PIN_SET_LOW(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                            CORE_AVR_PIN_SET_LOW(SR_DIN_LATCH_PORT, SR_DIN_LATCH_PIN);
                            _NOP();

                            CORE_AVR_PIN_SET_HIGH(SR_DIN_LATCH_PORT, SR_DIN_LATCH_PIN);

                            for (int j = 0; j < NUMBER_OF_BUTTON_ROWS; j++)
                            {
                                CORE_AVR_PIN_SET_LOW(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                                _NOP();
                                BIT_WRITE(digitalInBuffer[dIn_head][i], Board::map::inMatrixRow(j), !CORE_AVR_PIN_READ(SR_DIN_DATA_PORT, SR_DIN_DATA_PIN));
                                CORE_AVR_PIN_SET_HIGH(SR_DIN_CLK_PORT, SR_DIN_CLK_PIN);
                            }
                        }
                    }
#else
                    namespace
                    {
                        core::CORE_ARCH::pins::mcuPin_t pin;
                    }

                    inline void storeDigitalIn()
                    {
                        for (int i = 0; i < DIGITAL_IN_ARRAY_SIZE; i++)
                        {
                            for (int j = 0; j < 8; j++)
                            {
                                uint8_t buttonIndex = i * 8 + j;

                                if (buttonIndex >= MAX_NUMBER_OF_BUTTONS)
                                    break;    //done

                                pin = Board::map::button(buttonIndex);

                                BIT_WRITE(digitalInBuffer[dIn_head][i], j, !CORE_AVR_PIN_READ(*pin.port, pin.pin));
                            }
                        }
                    }
#endif

                    void checkDigitalInputs()
                    {
                        if (dIn_count < DIGITAL_IN_BUFFER_SIZE)
                        {
                            if (++dIn_head == DIGITAL_IN_BUFFER_SIZE)
                                dIn_head = 0;

                            storeDigitalIn();

                            dIn_count++;
                        }
                    }
                }    // namespace detail
            }        // namespace input
        }            // namespace digital
    }                // namespace interface
}    // namespace Board