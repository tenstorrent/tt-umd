from tenstorrent import utility

def main(chip, args):
    args = utility.parse_args(args)

    print ("Scratch registers:")
    chip.print_scratch_regs()

    chip.print_gpios()

    if chip.check_L2_FW() == 0:
        chip.print_mailbox_status()
        if chip.check_L2_FW(True) == 0:
            print (f"DDR training status: { 'OK' if chip.check_ddr_trained()==0 else 'ERROR' }")
        else:
            print ("DDR is not trained")
        # Get ARC FW versioning
        if chip.check_arc_fw_version() != 0:
            utility.PRINT_RED ("Could not get ARC FW version")
    else:
        utility.PRINT_RED ("L2 FW not loaded")
    return 0
