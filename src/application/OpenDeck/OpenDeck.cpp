/*
    OpenDeck MIDI platform firmware
    Copyright (C) 2015-2018 Igor Petrovic

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "OpenDeck.h"
#include "core/src/general/Timing.h"
#include "interface/CInfo.h"

#ifdef __AVR__
#include <util/atomic.h>

extern "C" void __cxa_pure_virtual()
{
    while (1);
}
#endif


cinfoHandler_t cinfoHandler;

void OpenDeck::init()
{
    Board::init();
    database.init();
    sysConfig.init();

    #ifdef __AVR__
    //enable global interrupts
    sei();
    #endif

    sysConfig.configureMIDI();
    Board::ledFlashStartup(Board::checkNewRevision());

    #ifdef LEDS_SUPPORTED
    leds.init();
    #endif

    #ifdef DISPLAY_SUPPORTED
    if (database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureEnable))
    {
        if (display.init(static_cast<displayController_t>(database.read(DB_BLOCK_DISPLAY, dbSection_display_hw, displayHwController)), static_cast<displayResolution_t>(database.read(DB_BLOCK_DISPLAY, dbSection_display_hw, displayHwResolution))))
        {
            display.setDirectWriteState(true);

            if (database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureWelcomeMsg))
                display.displayWelcomeMessage();

            if (database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureVInfoMsg))
                display.displayVinfo(false);

            display.setDirectWriteState(false);

            display.displayHome();
            display.setRetentionState(database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureMIDIeventRetention));
            display.setRetentionTime(database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureMIDIeventTime) * 1000);
            display.setAlternateNoteDisplay(database.read(DB_BLOCK_DISPLAY, dbSection_display_features, displayFeatureMIDInotesAlternate));
        }
    }
    #endif

    #ifdef BOARD_BERGAMOT
    touchscreen.init(ts_sdw);
    touchscreen.setPage(1);
    #endif

    static SysConfig *sysConfRef = nullptr;
    static Buttons   *buttonsRef = nullptr;

    sysConfRef = &sysConfig;
    buttonsRef = &buttons;

    cinfoHandler = [](dbBlockID_t dbBlock, sysExParameter_t componentID)
    {
        return sysConfRef->sendCInfo(dbBlock, componentID);
    };

    analog.setButtonHandler([](uint8_t analogIndex, uint16_t adcValue)
    {
        buttonsRef->processButton(analogIndex+MAX_NUMBER_OF_BUTTONS, buttonsRef->getStateFromAnalogValue(adcValue));
    });

    #ifdef TOUCHSCREEN_SUPPORTED
    touchscreen.setButtonHandler([](uint8_t index, bool state)
    {
        buttonsRef->processButton(MAX_NUMBER_OF_BUTTONS+MAX_NUMBER_OF_ANALOG+index, state);
    });
    #endif
}

void OpenDeck::checkComponents()
{
    if (sysConfig.isProcessingEnabled())
    {
        if (Board::digitalInputDataAvailable())
        {
            buttons.update();
            encoders.update();
        }

        analog.update();
        #ifdef LEDS_SUPPORTED
        leds.checkBlinking();
        #endif

        #ifdef DISPLAY_SUPPORTED
        display.update();
        #endif

        #ifdef TOUCHSCREEN_SUPPORTED
        touchscreen.update();
        #endif
    }
}

void OpenDeck::checkMIDI()
{
    auto processMessage = [this](midiInterfaceType_t interface)
    {
        //new message
        midiMessageType_t messageType = midi.getType(interface);
        uint8_t data1 = midi.getData1(interface);
        #if defined(LEDS_SUPPORTED) || defined(DISPLAY_SUPPORTED)
        uint8_t data2 = midi.getData2(interface);
        uint8_t channel = midi.getChannel(interface);
        #endif

        switch(messageType)
        {
            case midiMessageSystemExclusive:
            sysConfig.handleMessage(midi.getSysExArray(interface), midi.getSysExArrayLength(interface));
            break;

            case midiMessageNoteOn:
            case midiMessageNoteOff:
            case midiMessageControlChange:
            case midiMessageProgramChange:
            #if defined(LEDS_SUPPORTED) || defined(DISPLAY_SUPPORTED)
            if (messageType == midiMessageNoteOff)
                data2 = 0;
            #endif
            #ifdef LEDS_SUPPORTED
            leds.midiToState(messageType, data1, data2, channel);
            #endif
            #ifdef DISPLAY_SUPPORTED
            switch(messageType)
            {
                case midiMessageNoteOn:
                display.displayMIDIevent(displayEventIn, midiMessageNoteOn_display, data1, data2, channel+1);
                break;

                case midiMessageNoteOff:
                display.displayMIDIevent(displayEventIn, midiMessageNoteOff_display, data1, data2, channel+1);
                break;

                case midiMessageControlChange:
                display.displayMIDIevent(displayEventIn, midiMessageControlChange_display, data1, data2, channel+1);
                break;

                case midiMessageProgramChange:
                display.displayMIDIevent(displayEventIn, midiMessageProgramChange_display, data1, data2, channel+1);
                break;

                default:
                break;
            }
            #endif

            if (messageType == midiMessageProgramChange)
                database.setPreset(data1);
            break;

            case midiMessageClock:
            #ifdef LEDS_SUPPORTED
            leds.checkBlinking(true);
            #endif
            break;

            case midiMessageStart:
            #ifdef LEDS_SUPPORTED
            leds.resetBlinking();
            leds.checkBlinking(true);
            #endif
            break;

            default:
            break;
        }
    };

    //note: mega/uno
    //"fake" usbInterface - din data is stored as usb data so use usb callback to read the usb
    //packet stored in midi object
    if (midi.read(usbInterface))
        processMessage(usbInterface);

    #ifdef DIN_MIDI_SUPPORTED
    if (database.read(DB_BLOCK_GLOBAL, dbSection_global_midiFeatures, midiFeatureDinEnabled))
    {
        if (database.read(DB_BLOCK_GLOBAL, dbSection_global_midiFeatures, midiFeatureMergeEnabled))
        {
            switch(database.read(DB_BLOCK_GLOBAL, dbSection_global_midiMerge, midiMergeType))
            {
                case midiMergeDINtoUSB:
                //dump everything from DIN MIDI in to USB MIDI out
                midi.read(dinInterface, THRU_FULL_USB);
                break;

                // case midiMergeDINtoDIN:
                //loopback is automatically configured here
                // break;

                // case midiMergeODmaster:
                //already configured
                // break;

                case midiMergeODslave:
                //handle the traffic regulary until slave is properly configured
                //(upon receiving message from master)
                if (midi.read(dinInterface))
                    processMessage(dinInterface);
                break;

                default:
                break;
            }
        }
        else
        {
            if (midi.read(dinInterface))
                processMessage(dinInterface);
        }
    }
    #endif
}

void OpenDeck::update()
{
    checkMIDI();
    checkComponents();
}