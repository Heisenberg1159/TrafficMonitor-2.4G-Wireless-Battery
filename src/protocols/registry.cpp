// The list of supported receivers. Adding a device = one row here plus its
// provider file (see docs/ADDING_A_DEVICE.md).
#include "protocols.h"

const std::vector<ProtocolInfo>& Protocols() {
    // Order matters only when several receivers of the same kind are plugged
    // in at once: the first one found wins.
    static const std::vector<ProtocolInfo> kProtocols = {
        {"compx-mouse", DeviceKind::Mouse, L"Compx 2.4G mouse receiver (VID 3554, usage page FF02)",
         &CreateCompxMouseProvider},
        {"mchose-glw-keyboard", DeviceKind::Keyboard, L"MCHOSE glw/CZ 2.4G keyboard receiver (VID 3837)",
         &CreateMchoseKeyboardProvider},
        // Add new protocols here.
    };
    return kProtocols;
}
