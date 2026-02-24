# Tools

This folder contains standalone helper scripts used during debug/validation of generated AIE control-code artifacts.

## Scripts

### Data_Integrity_Validator.py

Validates consistency between `UC_DMA_BD` references and the corresponding data payload labels/entries in a generated `.asm` file.

What it checks (high-level):
- Every `UC_DMA_BD ... @LABEL, SIZE, ...` references a label that exists in the data section.
- The referenced label’s payload length (number of `.long` entries) matches the `SIZE` field.

Usage:
- `python tools/Data_Integrity_Validator.py <input.asm>`
- `python tools/Data_Integrity_Validator.py <input.asm> <output_report.txt>`

Example:
- `python tools/Data_Integrity_Validator.py aie_runtime_control0.asm`

Notes:
- The validator is intended to be run on generated assembly to catch missing/mismatched data labels early.
- This README is expected to grow as more scripts are added under `tools/`.
