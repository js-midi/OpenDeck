/*

Copyright 2015-2021 Igor Petrovic

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

#include "Buttons.h"
#include "io/common/Common.h"
#include "core/src/general/Helpers.h"
#include "core/src/general/Timing.h"

using namespace IO;

/// Continuously reads inputs from buttons and acts if necessary.
void Buttons::update()
{
    for (int i = 0; i < MAX_NUMBER_OF_BUTTONS; i++)
    {
        uint8_t  numberOfReadings = 0;
        uint32_t states           = 0;

        if (!_hwa.state(i, numberOfReadings, states))
            continue;

        uint32_t currentTime = core::timing::currentRunTimeMs();

        for (int reading = 0; reading < numberOfReadings; reading++)
        {
            //take into account that there is a 1ms difference between readouts
            //when processing, newest sample has index 0
            //start from oldest reading which is in upper bits
            uint8_t  processIndex = numberOfReadings - 1 - reading;
            uint32_t sampleTime   = currentTime - (TIME_DIFF_READOUT * processIndex);

            bool state = states >> processIndex;
            state &= 0x01;

            if (!_filter.isFiltered(i, state, state, sampleTime))
                continue;

            processButton(i, state);
        }
    }
}

/// Handles changes in button states.
/// param [in]: index       Button index which has changed state.
/// param [in]: newState    Latest button state.
void Buttons::processButton(size_t index, bool newState)
{
    //act on change of state only
    if (newState == state(index))
        return;

    setState(index, newState);

    buttonMessageDescriptor_t descriptor;

    descriptor.messageType = static_cast<messageType_t>(_database.read(Database::Section::button_t::midiMessage, index));
    descriptor.note        = _database.read(Database::Section::button_t::midiID, index);
    descriptor.channel     = _database.read(Database::Section::button_t::midiChannel, index);
    descriptor.velocity    = _database.read(Database::Section::button_t::velocity, index);

    auto type = static_cast<type_t>(_database.read(Database::Section::button_t::type, index));

    //don't process messageType_t::none type of message
    if (descriptor.messageType != messageType_t::none)
    {
        bool sendMIDI = true;

        //overwrite type under certain conditions
        switch (descriptor.messageType)
        {
        case messageType_t::programChange:
        case messageType_t::programChangeInc:
        case messageType_t::programChangeDec:
        case messageType_t::mmcPlay:
        case messageType_t::mmcStop:
        case messageType_t::mmcPause:
        case messageType_t::controlChange:
        case messageType_t::realTimeClock:
        case messageType_t::realTimeStart:
        case messageType_t::realTimeContinue:
        case messageType_t::realTimeStop:
        case messageType_t::realTimeActiveSensing:
        case messageType_t::realTimeSystemReset:
        case messageType_t::multiValIncResetNote:
        case messageType_t::multiValIncDecNote:
        case messageType_t::multiValIncResetCC:
        case messageType_t::multiValIncDecCC:
            type = type_t::momentary;
            break;

        case messageType_t::mmcRecord:
            type = type_t::latching;
            break;

        case messageType_t::presetOpenDeck:
            type     = type_t::momentary;
            sendMIDI = false;
            break;

        default:
            break;
        }

        if (type == type_t::latching)
        {
            //act on press only
            if (newState)
            {
                if (latchingState(index))
                {
                    setLatchingState(index, false);
                    //overwrite before processing
                    newState = false;
                }
                else
                {
                    setLatchingState(index, true);
                    newState = true;
                }
            }
            else
            {
                sendMIDI = false;
            }
        }

        if (sendMIDI)
        {
            sendMessage(index, newState, descriptor);
        }
        else if (descriptor.messageType == messageType_t::presetOpenDeck)
        {
            //change preset only on press
            if (newState)
            {
                uint8_t preset = _database.read(Database::Section::button_t::midiID, index);
                _database.setPreset(preset);
            }
        }
    }

    _cInfo.send(Database::block_t::buttons, index);
}

/// Used to send MIDI message from specified button.
/// Used internally once the button state has been changed and processed.
/// param [in]: index           Button index which sends the message.
/// param [in]: state           Button state (true/pressed, false/released).
/// param [in]: buttonMessage   Type of MIDI message to send. If unspecified, message type is read from _database.
void Buttons::sendMessage(size_t index, bool state, buttonMessageDescriptor_t& descriptor)
{
    if (descriptor.messageType == messageType_t::AMOUNT)
        descriptor.messageType = static_cast<messageType_t>(_database.read(Database::Section::button_t::midiMessage, index));

    _mmcArray[2] = descriptor.note;    //use midi note as channel id for transport control

    bool send = true;

    if (state)
    {
        switch (descriptor.messageType)
        {
        case messageType_t::note:
        {
            _midi.sendNoteOn(descriptor.note, descriptor.velocity, descriptor.channel);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::noteOn, descriptor.note, descriptor.velocity, descriptor.channel + 1);
            _leds.midiToState(MIDI::messageType_t::noteOn, descriptor.note, descriptor.velocity, descriptor.channel, LEDs::dataSource_t::internal);
        }
        break;

        case messageType_t::programChange:
        case messageType_t::programChangeInc:
        case messageType_t::programChangeDec:
        {
            if (descriptor.messageType != messageType_t::programChange)
            {
                if (descriptor.messageType == messageType_t::programChangeInc)
                {
                    if (!Common::pcIncrement(descriptor.channel))
                        send = false;
                }
                else
                {
                    if (!Common::pcDecrement(descriptor.channel))
                        send = false;
                }

                descriptor.note = Common::program(descriptor.channel);
            }

            if (send)
            {
                _midi.sendProgramChange(descriptor.note, descriptor.channel);
                _leds.midiToState(MIDI::messageType_t::programChange, descriptor.note, 0, descriptor.channel, LEDs::dataSource_t::internal);
                _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::programChange, descriptor.note, 0, descriptor.channel + 1);
            }
        }
        break;

        case messageType_t::controlChange:
        case messageType_t::controlChangeReset:
        {
            _midi.sendControlChange(descriptor.note, descriptor.velocity, descriptor.channel);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::controlChange, descriptor.note, descriptor.velocity, descriptor.channel + 1);
            _leds.midiToState(MIDI::messageType_t::controlChange, descriptor.note, descriptor.velocity, descriptor.channel, LEDs::dataSource_t::internal);
        }
        break;

        case messageType_t::mmcPlay:
        {
            _mmcArray[4] = 0x02;
            _midi.sendSysEx(6, _mmcArray, true);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::mmcPlay, _mmcArray[2], 0, 0);
        }
        break;

        case messageType_t::mmcStop:
        {
            _mmcArray[4] = 0x01;
            _midi.sendSysEx(6, _mmcArray, true);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::mmcStop, _mmcArray[2], 0, 0);
        }
        break;

        case messageType_t::mmcPause:
        {
            _mmcArray[4] = 0x09;
            _midi.sendSysEx(6, _mmcArray, true);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::mmcPause, _mmcArray[2], 0, 0);
        }
        break;

        case messageType_t::realTimeClock:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeClock);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeClock, 0, 0, 0);
        }
        break;

        case messageType_t::realTimeStart:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeStart);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeStart, 0, 0, 0);
        }
        break;

        case messageType_t::realTimeContinue:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeContinue);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeContinue, 0, 0, 0);
        }
        break;

        case messageType_t::realTimeStop:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeStop);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeStop, 0, 0, 0);
        }
        break;

        case messageType_t::realTimeActiveSensing:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeActiveSensing);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeActiveSensing, 0, 0, 0);
        }
        break;

        case messageType_t::realTimeSystemReset:
        {
            _midi.sendRealTime(MIDI::messageType_t::sysRealTimeSystemReset);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::sysRealTimeSystemReset, 0, 0, 0);
        }
        break;

        case messageType_t::mmcRecord:
        {
            //start recording
            _mmcArray[4] = 0x06;
            _midi.sendSysEx(6, _mmcArray, true);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::mmcRecordOn, _mmcArray[2], 0, 0);
        }
        break;

        case messageType_t::multiValIncResetNote:
        {
            uint8_t currentValue = Common::currentValue(index);
            uint8_t value        = Common::valueInc(index, descriptor.velocity, Common::incDecType_t::reset);

            if (currentValue != value)
            {
                if (!value)
                {
                    _midi.sendNoteOff(descriptor.note, value, descriptor.channel);
                    _leds.midiToState(MIDI::messageType_t::noteOff, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                    _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::noteOff, descriptor.note, value, descriptor.channel + 1);
                }
                else
                {
                    _midi.sendNoteOn(descriptor.note, value, descriptor.channel);
                    _leds.midiToState(MIDI::messageType_t::noteOn, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                    _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::noteOn, descriptor.note, value, descriptor.channel + 1);
                }
            }
        }
        break;

        case messageType_t::multiValIncDecNote:
        {
            uint8_t currentValue = Common::currentValue(index);
            uint8_t value        = Common::valueIncDec(index, descriptor.velocity);

            if (currentValue != value)
            {
                if (!value)
                {
                    _midi.sendNoteOff(descriptor.note, value, descriptor.channel);
                    _leds.midiToState(MIDI::messageType_t::noteOff, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                    _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::noteOff, descriptor.note, value, descriptor.channel + 1);
                }
                else
                {
                    _midi.sendNoteOn(descriptor.note, value, descriptor.channel);
                    _leds.midiToState(MIDI::messageType_t::noteOn, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                    _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::noteOn, descriptor.note, value, descriptor.channel + 1);
                }
            }
        }
        break;

        case messageType_t::multiValIncResetCC:
        {
            uint8_t currentValue = Common::currentValue(index);
            uint8_t value        = Common::valueInc(index, descriptor.velocity, Common::incDecType_t::reset);

            if (currentValue != value)
            {
                _midi.sendControlChange(descriptor.note, value, descriptor.channel);
                _leds.midiToState(MIDI::messageType_t::controlChange, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::controlChange, descriptor.note, value, descriptor.channel + 1);
            }
        }
        break;

        case messageType_t::multiValIncDecCC:
        {
            uint8_t currentValue = Common::currentValue(index);
            uint8_t value        = Common::valueIncDec(index, descriptor.velocity);

            if (currentValue != value)
            {
                _midi.sendControlChange(descriptor.note, value, descriptor.channel);
                _leds.midiToState(MIDI::messageType_t::controlChange, descriptor.note, value, descriptor.channel, LEDs::dataSource_t::internal);
                _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::controlChange, descriptor.note, value, descriptor.channel + 1);
            }
        }
        break;

        default:
            break;
        }
    }
    else
    {
        switch (descriptor.messageType)
        {
        case messageType_t::note:
            _midi.sendNoteOff(descriptor.note, 0, descriptor.channel);
            _display.displayMIDIevent(Display::eventType_t::out, _midi.getNoteOffMode() == MIDI::noteOffType_t::standardNoteOff ? Display::event_t::noteOff : Display::event_t::noteOn, descriptor.note, 0, descriptor.channel + 1);
            _leds.midiToState(MIDI::messageType_t::noteOff, descriptor.note, 0, descriptor.channel, LEDs::dataSource_t::internal);
            break;

        case messageType_t::controlChangeReset:
            _midi.sendControlChange(descriptor.note, 0, descriptor.channel);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::controlChange, descriptor.note, 0, descriptor.channel + 1);
            _leds.midiToState(MIDI::messageType_t::controlChange, descriptor.note, 0, descriptor.channel, LEDs::dataSource_t::internal);
            break;

        case messageType_t::mmcRecord:
            //stop recording
            _mmcArray[4] = 0x07;
            _midi.sendSysEx(6, _mmcArray, true);
            _display.displayMIDIevent(Display::eventType_t::out, Display::event_t::mmcRecordOff, _mmcArray[2], 0, 0);
            break;

        default:
            break;
        }
    }
}

/// Updates current state of button.
/// param [in]: index       Button for which state is being changed.
/// param [in]: state       New button state (true/pressed, false/released).
void Buttons::setState(size_t index, bool state)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t buttonIndex = index - 8 * arrayIndex;

    BIT_WRITE(_buttonPressed[arrayIndex], buttonIndex, state);
}

/// Checks for last button state.
/// param [in]: index    Button index for which previous state is being checked.
/// returns: True if last state was on/pressed, false otherwise.
bool Buttons::state(size_t index)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t buttonIndex = index - 8 * arrayIndex;

    return BIT_READ(_buttonPressed[arrayIndex], buttonIndex);
}

/// Updates current state of latching button.
/// Used only for latching buttons where new state which should be sent differs
/// from last one, for instance when sending MIDI note on on first press (latching
/// state: true), and note off on second (latching state: false).
/// State should be stored in variable because unlike momentary buttons, state of
/// latching buttons doesn't necessarrily match current "real" state of button since events
/// for latching buttons are sent only on presses.
/// param [in]: index    Button for which state is being changed.
/// param [in]: state       New latching state.
void Buttons::setLatchingState(size_t index, bool state)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t buttonIndex = index - 8 * arrayIndex;

    BIT_WRITE(_lastLatchingState[arrayIndex], buttonIndex, state);
}

/// Checks for last latching button state.
/// param [in]: index    Button index for which previous latching state is being checked.
/// returns: True if last state was on/pressed, false otherwise.
bool Buttons::latchingState(size_t index)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t buttonIndex = index - 8 * arrayIndex;

    return BIT_READ(_lastLatchingState[arrayIndex], buttonIndex);
}

/// Resets the current state of the specified button.
/// param [in]: index    Button for which to reset state.
void Buttons::reset(size_t index)
{
    setState(index, false);
    setLatchingState(index, false);
    _filter.reset(index);
}