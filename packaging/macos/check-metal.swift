// Hosted macOS VMs may have no GPU. Never report a graphics test as passed then.
import Metal
let devices = MTLCopyAllDevices()
for device in devices { print("Metal GPU: \(device.name)") }
if devices.isEmpty { print("No Metal GPU exposed by this runner.") }
exit(devices.isEmpty ? 2 : 0)
