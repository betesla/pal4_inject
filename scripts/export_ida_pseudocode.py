#!/usr/bin/env python3
"""
Bulk-export the current IDA database into a single C-like pseudocode file.

Usage inside IDA:
    File -> Script file... -> export_ida_pseudocode.py

Optional arguments through `idc.ARGV`:
    ARGV[1] = output file path

If no output path is provided, the script writes next to the input database as:
    <input_name>_all_functions.c
"""

from __future__ import annotations

import datetime
import os
import traceback

import ida_funcs
import ida_hexrays
import ida_kernwin
import ida_lines
import ida_nalt
import idaapi
import idautils
import idc


def _resolve_output_path() -> str:
    if len(idc.ARGV) >= 2 and idc.ARGV[1]:
        return os.path.abspath(idc.ARGV[1])

    input_path = ida_nalt.get_input_file_path()
    input_dir = os.path.dirname(input_path)
    root_name = ida_nalt.get_root_filename()
    return os.path.join(input_dir, f"{root_name}_all_functions.c")


def _write_header(out_file, function_count: int) -> None:
    timestamp = datetime.datetime.now().isoformat(timespec="seconds")
    out_file.write("/*\n")
    out_file.write(" * IDA pseudocode export\n")
    out_file.write(f" * Input: {ida_nalt.get_input_file_path()}\n")
    out_file.write(f" * Generated: {timestamp}\n")
    out_file.write(f" * Functions: {function_count}\n")
    out_file.write(" */\n\n")


def _decompile_function(ea: int) -> str:
    cfunc = ida_hexrays.decompile(ea)
    if cfunc is None:
        raise RuntimeError("Hex-Rays returned null")

    sv = cfunc.get_pseudocode()
    lines = []
    for line in sv:
        text = ida_lines.tag_remove(line.line)
        lines.append(text)
    return "\n".join(lines)


def main() -> None:
    if not ida_hexrays.init_hexrays_plugin():
        raise RuntimeError("Hex-Rays plugin is unavailable")

    output_path = _resolve_output_path()
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    function_eas = list(idautils.Functions())
    exported = 0
    failures = 0

    with open(output_path, "w", encoding="utf-8", newline="\n") as out_file:
        _write_header(out_file, len(function_eas))

        for index, ea in enumerate(function_eas, start=1):
            func = ida_funcs.get_func(ea)
            if func is None:
                continue

            name = idc.get_func_name(ea) or f"sub_{ea:08X}"
            out_file.write("/" + "*" * 78 + "\n")
            out_file.write(f" * #{index} {name} @ 0x{ea:08X}\n")
            out_file.write(" " + "*" * 78 + "/\n")

            try:
                pseudocode = _decompile_function(ea)
                out_file.write(pseudocode)
                if not pseudocode.endswith("\n"):
                    out_file.write("\n")
                exported += 1
            except Exception as exc:  # noqa: BLE001
                failures += 1
                out_file.write(f"/* decompile_failed: {exc} */\n")
                tb = traceback.format_exc().strip().replace("*/", "* /")
                out_file.write("/* traceback:\n")
                out_file.write(tb)
                out_file.write("\n*/\n")

            out_file.write("\n\n")
            if index % 50 == 0:
                ida_kernwin.replace_wait_box(
                    f"Exporting pseudocode... {index}/{len(function_eas)}")

    summary = (
        f"export_ida_pseudocode: wrote {output_path}\n"
        f"functions={len(function_eas)} exported={exported} failures={failures}"
    )
    print(summary)
    ida_kernwin.info(summary)


if __name__ == "__main__":
    ida_kernwin.show_wait_box("HIDECANCEL\nExporting Hex-Rays pseudocode...")
    try:
        main()
    finally:
        ida_kernwin.hide_wait_box()
