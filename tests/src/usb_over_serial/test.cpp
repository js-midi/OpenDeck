#ifdef USE_UART

#include "unity/Framework.h"
#include "board/Board.h"
#include "board/common/comm/USBOverSerial/USBOverSerial.h"
#include "core/src/general/RingBuffer.h"

#define TEST_MIDI_CHANNEL 0
#define BUFFER_SIZE       50

namespace
{
    core::RingBuffer<uint8_t, BUFFER_SIZE> buffer;
}    // namespace

namespace Board
{
    namespace UART
    {
        bool read(uint8_t channel, uint8_t& data)
        {
            return buffer.remove(data);
        }

        bool write(uint8_t channel, uint8_t data)
        {
            TEST_ASSERT(buffer.insert(data) == true);
            return true;
        }
    }    // namespace UART
}    // namespace Board

TEST_SETUP()
{
    buffer.reset();
}

TEST_CASE(MIDI)
{
    using namespace Board;

    std::array<uint8_t, 4> dataBufRecv;
    std::vector<uint8_t>   dataBufSend;

    dataBufSend.push_back(static_cast<uint8_t>(MIDI::messageType_t::noteOn));
    dataBufSend.push_back(0x10);
    dataBufSend.push_back(0x7D);
    dataBufSend.push_back(0x30);

    USBOverSerial::USBWritePacket sending(USBOverSerial::packetType_t::midi, &dataBufSend[0], dataBufSend.size());
    USBOverSerial::USBReadPacket  receiving(&dataBufRecv[0], dataBufRecv.size());

    TEST_ASSERT(USBOverSerial::write(TEST_MIDI_CHANNEL, sending) == true);
    TEST_ASSERT(USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);

    TEST_ASSERT(receiving.type() == USBOverSerial::packetType_t::midi);

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(MIDI::messageType_t::noteOn), receiving[0]);
    TEST_ASSERT_EQUAL_UINT32(0x10, receiving[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7D, receiving[2]);
    TEST_ASSERT_EQUAL_UINT32(0x30, receiving[3]);

    //packet is done - read should return true and not cause any changes in the data until
    //.reset() is called
    TEST_ASSERT(USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(MIDI::messageType_t::noteOn), receiving[0]);
    TEST_ASSERT_EQUAL_UINT32(0x10, receiving[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7D, receiving[2]);
    TEST_ASSERT_EQUAL_UINT32(0x30, receiving[3]);

    //now do the same, but this time add some bytes of junk before writing the actual packet
    //verify that this doesn't cause any issues and that the packet is read again correctly
    TEST_ASSERT(buffer.insert(0x7E) == true);
    TEST_ASSERT(buffer.insert(0x7D) == true);
    TEST_ASSERT(buffer.insert(0x7D) == true);
    TEST_ASSERT(buffer.insert(0x7D) == true);
    TEST_ASSERT(buffer.insert(0x7D) == true);
    TEST_ASSERT(buffer.insert(0x7E) == true);

    receiving.reset();

    TEST_ASSERT(USBOverSerial::write(TEST_MIDI_CHANNEL, sending) == true);
    TEST_ASSERT(USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);

    TEST_ASSERT(receiving.type() == USBOverSerial::packetType_t::midi);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(MIDI::messageType_t::noteOn), receiving[0]);
    TEST_ASSERT_EQUAL_UINT32(0x10, receiving[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7D, receiving[2]);
    TEST_ASSERT_EQUAL_UINT32(0x30, receiving[3]);

    receiving.reset();
    buffer.reset();

    //actual case: send boundary, command type and then restart the packet again
    TEST_ASSERT(buffer.insert(0x7E) == true);
    TEST_ASSERT(buffer.insert(static_cast<uint8_t>(USBOverSerial::packetType_t::midi)) == true);
    TEST_ASSERT(buffer.insert(0x7E) == true);
    TEST_ASSERT(buffer.insert(static_cast<uint8_t>(USBOverSerial::packetType_t::midi)) == true);
    TEST_ASSERT(buffer.insert(0x01) == true);
    TEST_ASSERT(buffer.insert(0x01) == true);

    TEST_ASSERT(USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);
}

TEST_CASE(InternalCMD)
{
    using namespace Board;

    std::array<uint8_t, 4> dataBufRecv;
    std::vector<uint8_t>   dataBufSend;

    dataBufSend.push_back(0x7D);
    dataBufSend.push_back(0x7E);
    dataBufSend.push_back(0x7E);
    dataBufSend.push_back(0x30);

    USBOverSerial::USBWritePacket sending(USBOverSerial::packetType_t::internal, &dataBufSend[0], dataBufSend.size());
    USBOverSerial::USBReadPacket  receiving(&dataBufRecv[0], dataBufRecv.size());

    TEST_ASSERT(USBOverSerial::write(TEST_MIDI_CHANNEL, sending) == true);
    TEST_ASSERT(Board::USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);

    TEST_ASSERT(receiving.type() == USBOverSerial::packetType_t::internal);

    TEST_ASSERT_EQUAL_UINT32(0x7D, receiving[0]);
    TEST_ASSERT_EQUAL_UINT32(0x7E, receiving[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7E, receiving[2]);
    TEST_ASSERT_EQUAL_UINT32(0x30, receiving[3]);

    //packet is done - read should return true and not cause any changes in the data until
    //.reset() is called
    TEST_ASSERT(USBOverSerial::read(TEST_MIDI_CHANNEL, receiving) == true);
    TEST_ASSERT_EQUAL_UINT32(0x7D, receiving[0]);
    TEST_ASSERT_EQUAL_UINT32(0x7E, receiving[1]);
    TEST_ASSERT_EQUAL_UINT32(0x7E, receiving[2]);
    TEST_ASSERT_EQUAL_UINT32(0x30, receiving[3]);
}

#endif