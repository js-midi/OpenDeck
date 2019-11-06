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
#include "board/common/io/Helpers.h"
#include "board/Internal.h"
#include "core/src/general/Helpers.h"
#include "core/src/general/Atomic.h"
#include "Pins.h"
#include "interface/digital/output/leds/Helpers.h"
#include "interface/digital/output/leds/LEDs.h"

namespace
{
#if !defined(NUMBER_OF_OUT_SR) || defined(NUMBER_OF_LED_COLUMNS)
    core::io::mcuPin_t pin;
#endif
    uint8_t ledStateSingle;

#ifdef LED_FADING
    volatile uint8_t pwmSteps;
    volatile int8_t  transitionCounter[MAX_NUMBER_OF_LEDS];

    ///
    /// \brief Array holding values needed to achieve more natural LED transition between states.
    ///
    const uint8_t ledTransitionScale[NUMBER_OF_LED_TRANSITIONS] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        1,
        1,
        1,
        2,
        2,
        2,
        2,
        3,
        3,
        4,
        4,
        5,
        5,
        6,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        16,
        17,
        19,
        21,
        23,
        25,
        28,
        30,
        33,
        36,
        40,
        44,
        48,
        52,
        57,
        62,
        68,
        74,
        81,
        89,
        97,
        106,
        115,
        126,
        138,
        150,
        164,
        179,
        195,
        213,
        255
    };
#endif

#ifndef NUMBER_OF_LED_COLUMNS
    uint8_t lastLEDstate[MAX_NUMBER_OF_LEDS];
#else
    uint8_t ledNumber;

    ///
    /// \brief Holds value of currently active output matrix column.
    ///
    volatile uint8_t activeOutColumn;

    ///
    /// \brief Switches to next LED matrix column.
    ///
    inline void activateOutputColumn()
    {
        BIT_READ(activeOutColumn, 0) ? CORE_IO_SET_HIGH(DEC_LM_A0_PORT, DEC_LM_A0_PIN) : CORE_IO_SET_LOW(DEC_LM_A0_PORT, DEC_LM_A0_PIN);
        BIT_READ(activeOutColumn, 1) ? CORE_IO_SET_HIGH(DEC_LM_A1_PORT, DEC_LM_A1_PIN) : CORE_IO_SET_LOW(DEC_LM_A1_PORT, DEC_LM_A1_PIN);
        BIT_READ(activeOutColumn, 2) ? CORE_IO_SET_HIGH(DEC_LM_A2_PORT, DEC_LM_A2_PIN) : CORE_IO_SET_LOW(DEC_LM_A2_PORT, DEC_LM_A2_PIN);
    }

    ///
    /// \brief Used to turn the given LED row off.
    ///
    inline void ledRowOff(uint8_t row)
    {
#ifdef LED_FADING
        //turn off pwm
        core::io::pwmOff(Board::detail::map::pwmChannel(row));
#endif

        pin = Board::detail::map::led(row);
        EXT_LED_OFF(CORE_IO_MCU_PIN_PORT(pin), CORE_IO_MCU_PIN_INDEX(pin));
    }

    ///
    /// \brief Used to turn the given LED row on.
    /// If led fading is supported on board, intensity must be specified as well.
    ///
    inline void ledRowOn(uint8_t row
#ifdef LED_FADING
                         ,
                         uint8_t intensity
#endif
    )
    {
#ifdef LED_FADING
        if (intensity == 255)
#endif
        {
            pin = Board::detail::map::led(row);

            //max value, don't use pwm
            EXT_LED_ON(CORE_IO_MCU_PIN_PORT(pin), CORE_IO_MCU_PIN_INDEX(pin));
        }
#ifdef LED_FADING
        else
        {
#ifdef LED_EXT_INVERT
            intensity = 255 - intensity;
#endif

            core::io::pwmOn(Board::detail::map::pwmChannel(row), intensity);
        }
#endif
    }
#endif
}    // namespace

namespace Board
{
    namespace io
    {
#ifdef LED_FADING
        bool setLEDfadeSpeed(uint8_t transitionSpeed)
        {
            if (transitionSpeed > FADE_TIME_MAX)
            {
                return false;
            }

            //reset transition counter
            ATOMIC_SECTION
            {
                for (int i = 0; i < MAX_NUMBER_OF_LEDS; i++)
                    transitionCounter[i] = 0;

                pwmSteps = transitionSpeed;
            }

            return true;
        }
#endif

        uint8_t getRGBaddress(uint8_t rgbID, Interface::digital::output::LEDs::rgbIndex_t index)
        {
#ifdef NUMBER_OF_LED_COLUMNS
            uint8_t column = rgbID % NUMBER_OF_LED_COLUMNS;
            uint8_t row = (rgbID / NUMBER_OF_LED_COLUMNS) * 3;
            uint8_t address = column + NUMBER_OF_LED_COLUMNS * row;

            switch (index)
            {
            case Interface::digital::output::LEDs::rgbIndex_t::r:
                return address;

            case Interface::digital::output::LEDs::rgbIndex_t::g:
                return address + NUMBER_OF_LED_COLUMNS * 1;

            case Interface::digital::output::LEDs::rgbIndex_t::b:
                return address + NUMBER_OF_LED_COLUMNS * 2;
            }

            return 0;
#else
            return rgbID * 3 + static_cast<uint8_t>(index);
#endif
        }

        uint8_t getRGBID(uint8_t ledID)
        {
#ifdef NUMBER_OF_LED_COLUMNS
            uint8_t row = ledID / NUMBER_OF_LED_COLUMNS;

            uint8_t mod = row % 3;
            row -= mod;

            uint8_t column = ledID % NUMBER_OF_LED_COLUMNS;

            uint8_t result = (row * NUMBER_OF_LED_COLUMNS) / 3 + column;

            if (result >= MAX_NUMBER_OF_RGB_LEDS)
                return MAX_NUMBER_OF_RGB_LEDS - 1;
            else
                return result;
#else
            uint8_t result = ledID / 3;

            if (result >= MAX_NUMBER_OF_RGB_LEDS)
                return MAX_NUMBER_OF_RGB_LEDS - 1;
            else
                return result;
#endif
        }
    }    // namespace io

    namespace detail
    {
        namespace io
        {
#ifdef NUMBER_OF_LED_COLUMNS
            void checkDigitalOutputs()
            {
                for (int i = 0; i < NUMBER_OF_LED_ROWS; i++)
                    ledRowOff(i);

                activateOutputColumn();

                //if there is an active LED in current column, turn on LED row
                //do fancy transitions here
                for (int i = 0; i < NUMBER_OF_LED_ROWS; i++)
                {
                    ledNumber = activeOutColumn + i * NUMBER_OF_LED_COLUMNS;
                    ledStateSingle = LED_ON(Interface::digital::output::LEDs::getLEDstate(ledNumber));

                    ledStateSingle *= (NUMBER_OF_LED_TRANSITIONS - 1);

                    //don't bother with pwm if it's disabled
                    if (!pwmSteps && ledStateSingle)
                    {
                        ledRowOn(i
#ifdef LED_FADING
                                 ,
                                 255
#endif
                        );
                    }
                    else
                    {
                        if (ledTransitionScale[transitionCounter[ledNumber]])
                            ledRowOn(i
#ifdef LED_FADING
                                     ,
                                     ledTransitionScale[transitionCounter[ledNumber]]
#endif
                            );

                        if (transitionCounter[ledNumber] != ledStateSingle)
                        {
                            //fade up
                            if (transitionCounter[ledNumber] < ledStateSingle)
                            {
                                transitionCounter[ledNumber] += pwmSteps;

                                if (transitionCounter[ledNumber] > ledStateSingle)
                                    transitionCounter[ledNumber] = ledStateSingle;
                            }
                            else
                            {
                                //fade down
                                transitionCounter[ledNumber] -= pwmSteps;

                                if (transitionCounter[ledNumber] < 0)
                                    transitionCounter[ledNumber] = 0;
                            }
                        }
                    }
                }

                if (++activeOutColumn == NUMBER_OF_LED_COLUMNS)
                    activeOutColumn = 0;
            }
#elif defined(NUMBER_OF_OUT_SR)
            ///
            /// \brief Checks if any LED state has been changed and writes changed state to output shift registers.
            ///
            void checkDigitalOutputs()
            {
                bool updateSR = false;

                for (int i = 0; i < MAX_NUMBER_OF_LEDS; i++)
                {
                    ledStateSingle = LED_ON(Interface::digital::output::LEDs::getLEDstate(i));

                    if (ledStateSingle != lastLEDstate[i])
                    {
                        lastLEDstate[i] = ledStateSingle;
                        updateSR = true;
                    }
                }

                if (updateSR)
                {
                    CORE_IO_SET_LOW(SR_OUT_LATCH_PORT, SR_OUT_LATCH_PIN);

                    uint8_t ledIndex;

                    for (int j = 0; j < NUMBER_OF_OUT_SR; j++)
                    {
                        for (int i = 0; i < NUMBER_OF_OUT_SR_INPUTS; i++)
                        {
                            ledIndex = i + j * NUMBER_OF_OUT_SR_INPUTS;

                            LED_ON(Interface::digital::output::LEDs::getLEDstate(ledIndex)) ? EXT_LED_ON(SR_OUT_DATA_PORT, SR_OUT_DATA_PIN) : EXT_LED_OFF(SR_OUT_DATA_PORT, SR_OUT_DATA_PIN);
                            CORE_IO_SET_LOW(SR_OUT_CLK_PORT, SR_OUT_CLK_PIN);
                            _NOP();
                            _NOP();
                            CORE_IO_SET_HIGH(SR_OUT_CLK_PORT, SR_OUT_CLK_PIN);
                        }
                    }

                    CORE_IO_SET_HIGH(SR_OUT_LATCH_PORT, SR_OUT_LATCH_PIN);
                }
            }
#else
            void checkDigitalOutputs()
            {
                for (int i = 0; i < MAX_NUMBER_OF_LEDS; i++)
                {
                    ledStateSingle = LED_ON(Interface::digital::output::LEDs::getLEDstate(i));
                    pin = Board::detail::map::led(i);

                    if (ledStateSingle != lastLEDstate[i])
                    {
                        if (ledStateSingle)
                            EXT_LED_ON(CORE_IO_MCU_PIN_PORT(pin), CORE_IO_MCU_PIN_INDEX(pin));
                        else
                            EXT_LED_OFF(CORE_IO_MCU_PIN_PORT(pin), CORE_IO_MCU_PIN_INDEX(pin));

                        lastLEDstate[i] = ledStateSingle;
                    }
                }
            }
#endif
        }    // namespace io
    }        // namespace detail
}    // namespace Board