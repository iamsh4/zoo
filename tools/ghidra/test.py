from collections import defaultdict
import ghidra_bridge
bridge = ghidra_bridge.GhidraBridge(namespace=globals())

###################################

NAMESPACE = 'penguin'
DC_REGISTERS = [
    ("SB_ISTNRM", 0xa05f6900),
    ("SB_ISTEXT", 0xa05f6904),
    ("SB_ISTERR", 0xa05f6908),
]

start()

# Find occurrences of registers, generate labels for each
dc_register_addr_to_labels = defaultdict(list)
for name, reg_address in DC_REGISTERS:
    address_bytes = reg_address.to_bytes(4, "little")
    results = findBytes(None, address_bytes, 1000, 4)
    for result in results:
        print(
            f"Found instance of '{name}' (MMIO 0x{reg_address:08x}) @ 0x{result.offset:08x}")
        new_label = createLabel(result, name, False)
        dc_register_addr_to_labels[reg_address].append(new_label)

# Find references to all these labels
label_id_to_references = defaultdict(list)
for reg_addr, labels in dc_register_addr_to_labels.items():
    for label in labels:
        refs = getReferencesTo(label.address)
        if len(refs) > 0:
            for ref in refs:
                print(
                    f"Found reference from 0x{str(ref.fromAddress)} to MMIO register 0x{reg_addr:08x} stored @ 0x{label.address.offset:08x}")

# TODO : if references in code,
#   mark constant, set type

end(True)
