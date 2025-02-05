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

#include "board/common/comm/usb/USB.h"
#include "usbd_core.h"
#include "midi/src/MIDI.h"
#include "core/src/general/RingBuffer.h"
#include "core/src/general/Atomic.h"
#include "core/src/general/Timing.h"
#include "core/src/general/StringBuilder.h"
#include "board/Board.h"
#include "board/Internal.h"

/// Buffer size in bytes for incoming and outgoing MIDI messages (from device standpoint).

#define RX_BUFFER_SIZE_RING 4096

namespace
{
    USBD_HandleTypeDef                             _usbHandler;
    volatile uint8_t                               _midiRxBuffer[MIDI_IN_OUT_EPSIZE];
    volatile Board::detail::USB::txState_t         _txStateMIDI;
    core::RingBuffer<uint8_t, RX_BUFFER_SIZE_RING> _midiRxBufferRing;

    uint8_t initCallback(USBD_HandleTypeDef* pdev, uint8_t cfgidx)
    {
        USBD_LL_OpenEP(pdev, MIDI_STREAM_IN_EPADDR, USB_EP_TYPE_BULK, MIDI_IN_OUT_EPSIZE);
        USBD_LL_OpenEP(pdev, MIDI_STREAM_OUT_EPADDR, USB_EP_TYPE_BULK, MIDI_IN_OUT_EPSIZE);
        USBD_LL_PrepareReceive(pdev, MIDI_STREAM_OUT_EPADDR, (uint8_t*)(_midiRxBuffer), MIDI_IN_OUT_EPSIZE);

        _txStateMIDI = Board::detail::USB::txState_t::done;

        return USBD_OK;
    }

    uint8_t deInitCallback(USBD_HandleTypeDef* pdev, uint8_t cfgidx)
    {
        USBD_LL_CloseEP(pdev, MIDI_STREAM_IN_EPADDR);
        USBD_LL_CloseEP(pdev, MIDI_STREAM_OUT_EPADDR);

        return USBD_OK;
    }

    uint8_t dataInCallback(USBD_HandleTypeDef* pdev, uint8_t epnum)
    {
        _txStateMIDI = Board::detail::USB::txState_t::done;

        return USBD_OK;
    }

    uint8_t dataOutCallback(USBD_HandleTypeDef* pdev, uint8_t epnum)
    {
        uint32_t count = ((PCD_HandleTypeDef*)pdev->pData)->OUT_ep[epnum].xfer_count;

        for (uint32_t i = 0; i < count; i++)
            _midiRxBufferRing.insert(_midiRxBuffer[i]);

        return USBD_LL_PrepareReceive(pdev, MIDI_STREAM_OUT_EPADDR, (uint8_t*)(_midiRxBuffer), MIDI_IN_OUT_EPSIZE);
    }

    uint8_t* getDeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_Device_t* desc = USBgetDeviceDescriptor(length);
        return (uint8_t*)desc;
    }

    uint8_t* getLangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_String_t* desc = USBgetLanguageString(length);
        return (uint8_t*)desc;
    }

    uint8_t* getManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_String_t* desc = USBgetManufacturerString(length);
        return (uint8_t*)desc;
    }

    uint8_t* getProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_String_t* desc = USBgetProductString(length);
        return (uint8_t*)desc;
    }

    uint8_t* getSerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);

        Board::uniqueID_t uid;
        Board::uniqueID(uid);

        const USB_Descriptor_UID_String_t* desc = USBgetSerialIDString(length, &uid[0]);
        return (uint8_t*)desc;
    }

    uint8_t* getConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_String_t* desc = USBgetManufacturerString(length);
        return (uint8_t*)desc;
    }

    uint8_t* getInterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t* length)
    {
        UNUSED(speed);
        const USB_Descriptor_String_t* desc = USBgetManufacturerString(length);
        return (uint8_t*)desc;
    }

    uint8_t* getConfigDescriptor(uint16_t* length)
    {
        const USB_Descriptor_Configuration_t* cfg = USBgetCfgDescriptor(length);
        return (uint8_t*)cfg;
    }

    USBD_DescriptorsTypeDef DeviceDescriptor = {
        getDeviceDescriptor,
        getLangIDStrDescriptor,
        getManufacturerStrDescriptor,
        getProductStrDescriptor,
        getSerialStrDescriptor,
        getConfigStrDescriptor,
        getInterfaceStrDescriptor
    };

    USBD_ClassTypeDef USB_MIDI = {
        initCallback,
        deInitCallback,
        NULL,
        NULL,
        NULL,
        dataInCallback,
        dataOutCallback,
        NULL,
        NULL,
        NULL,
        NULL,
        getConfigDescriptor,
        NULL,
        NULL,
    };
}    // namespace

namespace Board
{
    namespace detail
    {
        namespace setup
        {
            void usb()
            {
                USBD_Init(&_usbHandler, &DeviceDescriptor, DEVICE_FS);
                USBD_RegisterClass(&_usbHandler, &USB_MIDI);
                USBD_Start(&_usbHandler);
            }
        }    // namespace setup
    }        // namespace detail

    namespace USB
    {
        bool isUSBconnected()
        {
            return (_usbHandler.dev_state == USBD_STATE_CONFIGURED);
        }

        bool readMIDI(MIDI::USBMIDIpacket_t& USBMIDIpacket)
        {
            bool returnValue = false;

            if (_midiRxBufferRing.count() >= 4)
            {
                _midiRxBufferRing.remove(USBMIDIpacket.Event);
                _midiRxBufferRing.remove(USBMIDIpacket.Data1);
                _midiRxBufferRing.remove(USBMIDIpacket.Data2);
                _midiRxBufferRing.remove(USBMIDIpacket.Data3);

                returnValue = true;
            }

            return returnValue;
        }

        bool writeMIDI(MIDI::USBMIDIpacket_t& USBMIDIpacket)
        {
            if (!isUSBconnected())
                return false;

            if (_txStateMIDI != Board::detail::USB::txState_t::done)
            {
                if (_txStateMIDI == Board::detail::USB::txState_t::waiting)
                {
                    return false;
                }
                else
                {
                    uint32_t currentTime = core::timing::currentRunTimeMs();

                    while ((core::timing::currentRunTimeMs() - currentTime) < USB_TX_TIMEOUT_MS)
                    {
                        if (_txStateMIDI == Board::detail::USB::txState_t::done)
                            break;
                    }

                    if (_txStateMIDI != Board::detail::USB::txState_t::done)
                        _txStateMIDI = Board::detail::USB::txState_t::waiting;
                }
            }

            if (_txStateMIDI == Board::detail::USB::txState_t::done)
            {
                _txStateMIDI = Board::detail::USB::txState_t::sending;
                return USBD_LL_Transmit(&_usbHandler, MIDI_STREAM_IN_EPADDR, (uint8_t*)&USBMIDIpacket, 4) == USBD_OK;
            }

            return false;
        }
    }    // namespace USB
}    // namespace Board