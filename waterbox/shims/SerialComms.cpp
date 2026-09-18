/* The Super Serial Card with nothing on the other end of the cable.
 *
 * Upstream's SerialComms.cpp is a COM port and a TCP socket on a thread; a
 * sandbox has neither. The card still needs to exist - a configuration can
 * name it in a slot, and a savestate from AppleWin can carry one - so this is
 * the class with its 6551 registers reset and no I/O: reads return the
 * floating bus, writes go nowhere. (Its snapshot loader mirrors upstream's
 * key layout so an .aws with an SSC still parses.)
 */
#include "StdAfx.h"

#include "SerialComms.h"
#include "Memory.h"
#include "YamlHelper.h"

const UINT CSuperSerialCard::SERIALPORTITEM_INVALID_COM_PORT = 0;
SSC_DIPSW CSuperSerialCard::m_DIPSWDefault;

CSuperSerialCard::CSuperSerialCard(UINT slot)
    : Card(CT_SSC, slot)
    , m_dwSerialPortItem(0)
    , m_uTCPChoiceItemIdx(0)
    , m_DIPSWCurrent()
    , m_uBaudRate(m_kDefaultBaudRate)
    , m_uStopBits(0)
    , m_uByteSize(8)
    , m_uParity(0)
    , m_uControlByte(0)
    , m_uCommandByte(0)
    , m_hCommHandle(INVALID_HANDLE_VALUE)
    , m_hCommListenSocket(INVALID_SOCKET)
    , m_hCommAcceptSocket(INVALID_SOCKET)
    , m_hFrameWindow(nullptr)
    , m_CriticalSection(nullptr)
    , m_vuRxCurrBuffer(0)
    , m_bTxIrqEnabled(false)
    , m_bRxIrqEnabled(false)
    , m_vbTxIrqPending(false)
    , m_vbRxIrqPending(false)
    , m_vbTxEmpty(true)
    , m_hCommThread(INVALID_HANDLE_VALUE)
    , m_o(nullptr)
    , m_pExpansionRom(nullptr)
    , m_bCfgSupportDCD(false)
    , m_uDTR(0)
    , m_dwModemStatus(m_kDefaultModemStatus)
    , m_uRTS(0)
{
    for (UINT i = 0; i < COMMEVT_MAX; i++)
        m_hCommEvent[i] = nullptr;
}

CSuperSerialCard::~CSuperSerialCard()
{
}

void CSuperSerialCard::SetSerialPortName(const char *)
{
}

void CSuperSerialCard::Reset(const bool)
{
    m_uControlByte = 0;
    m_uCommandByte = 0;
    m_vbTxIrqPending = false;
    m_vbRxIrqPending = false;
    m_vbTxEmpty = true;
}

void CSuperSerialCard::InitializeIO(LPBYTE)
{
}

void CSuperSerialCard::RescanCOMPortsAndSetSerialPortItem(DWORD)
{
}

// Unit version history:
// 2: Added: Support DCD flag
//    Removed: redundant data (encapsulated in Command & Control bytes)
static const UINT kUNIT_VERSION = 2;

#define SS_YAML_VALUE_CARD_SSC "Super Serial Card"

#define SS_YAML_KEY_DIPSWDEFAULT "DIPSW Default"
#define SS_YAML_KEY_DIPSWCURRENT "DIPSW Current"

#define SS_YAML_KEY_BAUDRATE "Baud Rate"
#define SS_YAML_KEY_FWMODE "Firmware mode"
#define SS_YAML_KEY_STOPBITS "Stop Bits"
#define SS_YAML_KEY_BYTESIZE "Byte Size"
#define SS_YAML_KEY_PARITY "Parity"
#define SS_YAML_KEY_LINEFEED "Linefeed"
#define SS_YAML_KEY_INTERRUPTS "Interrupts"
#define SS_YAML_KEY_CONTROL "Control Byte"
#define SS_YAML_KEY_COMMAND "Command Byte"
#define SS_YAML_KEY_INACTIVITY "Comm Inactivity"
#define SS_YAML_KEY_TXIRQENABLED "TX IRQ Enabled"
#define SS_YAML_KEY_RXIRQENABLED "RX IRQ Enabled"
#define SS_YAML_KEY_TXIRQPENDING "TX IRQ Pending"
#define SS_YAML_KEY_RXIRQPENDING "RX IRQ Pending"
#define SS_YAML_KEY_WRITTENTX "Written TX"
#define SS_YAML_KEY_SERIALPORTNAME "Serial Port Name"
#define SS_YAML_KEY_SUPPORT_DCD "Support DCD"

void CSuperSerialCard::LoadSnapshotDIPSW(YamlLoadHelper &yamlLoadHelper, std::string key, SSC_DIPSW &)
{
    if (!yamlLoadHelper.GetSubMap(key))
        throw std::runtime_error("Card: Expected key: " + key);

    yamlLoadHelper.LoadUint(SS_YAML_KEY_BAUDRATE);
    yamlLoadHelper.LoadUint(SS_YAML_KEY_FWMODE);
    yamlLoadHelper.LoadUint(SS_YAML_KEY_STOPBITS);
    yamlLoadHelper.LoadUint(SS_YAML_KEY_BYTESIZE);
    yamlLoadHelper.LoadUint(SS_YAML_KEY_PARITY);
    yamlLoadHelper.LoadBool(SS_YAML_KEY_LINEFEED);
    yamlLoadHelper.LoadBool(SS_YAML_KEY_INTERRUPTS);

    yamlLoadHelper.PopMap();
}

bool CSuperSerialCard::LoadSnapshot(YamlLoadHelper &yamlLoadHelper, UINT version)
{
    if (version < 1 || version > kUNIT_VERSION)
        ThrowErrorInvalidVersion(version);

    SSC_DIPSW dipsw;
    LoadSnapshotDIPSW(yamlLoadHelper, SS_YAML_KEY_DIPSWDEFAULT, dipsw);
    LoadSnapshotDIPSW(yamlLoadHelper, SS_YAML_KEY_DIPSWCURRENT, dipsw);

    if (version == 1)
    {
        yamlLoadHelper.LoadUint(SS_YAML_KEY_PARITY);
        yamlLoadHelper.LoadBool(SS_YAML_KEY_TXIRQENABLED);
        yamlLoadHelper.LoadBool(SS_YAML_KEY_RXIRQENABLED);
        yamlLoadHelper.LoadUint(SS_YAML_KEY_BAUDRATE);
        yamlLoadHelper.LoadUint(SS_YAML_KEY_STOPBITS);
        yamlLoadHelper.LoadUint(SS_YAML_KEY_BYTESIZE);
        yamlLoadHelper.LoadUint(SS_YAML_KEY_INACTIVITY);
    }
    else if (version >= 2)
    {
        yamlLoadHelper.LoadBool(SS_YAML_KEY_SUPPORT_DCD);
    }

    yamlLoadHelper.LoadUint(SS_YAML_KEY_COMMAND);
    yamlLoadHelper.LoadUint(SS_YAML_KEY_CONTROL);

    yamlLoadHelper.LoadBool(SS_YAML_KEY_TXIRQPENDING);
    yamlLoadHelper.LoadBool(SS_YAML_KEY_RXIRQPENDING);
    yamlLoadHelper.LoadBool(SS_YAML_KEY_WRITTENTX);

    yamlLoadHelper.LoadString(SS_YAML_KEY_SERIALPORTNAME);

    return true;
}

void CSuperSerialCard::SaveSnapshot(YamlSaveHelper &)
{
}

const std::string &CSuperSerialCard::GetSnapshotCardName()
{
    static const std::string name(SS_YAML_VALUE_CARD_SSC);
    return name;
}
