# Building QBE with OpenWatcom for DOS

This guide explains how to compile QBE using OpenWatcom v2 for 16-bit DOS real mode.

## Important Limitations

**This is an experimental port with significant limitations:**

1. **16-bit host, 64-bit target paradox**: You're compiling a 16-bit DOS executable that generates code for 64-bit architectures (amd64, arm64, rv64). The compiler itself runs in 16-bit real mode with severe memory constraints.

2. **Memory constraints**: Even with the huge memory model, DOS real mode is limited to 640KB of conventional memory. QBE's data structures are designed for modern systems and may cause out-of-memory errors.

3. **64-bit integer operations**: While OpenWatcom supports `long long` types, 64-bit arithmetic on 16-bit hardware is slow and uses significant code space.

4. **Pointer size mismatch**: DOS uses segmented 16:16 pointers (huge model = 32-bit far pointers), while the code was designed for flat 64-bit address spaces.

## Prerequisites

1. OpenWatcom v2 installed and configured
2. Environment variables set (run `source mysetvars.sh` from OpenWatcom directory)

## Files Modified/Created for DOS Compatibility

### New Files:
- `dosgetopt.h` - DOS getopt() header
- `dosgetopt.c` - DOS getopt() implementation
- `watcom.mak` - OpenWatcom makefile
- `config.h` - Configuration header (normally generated)
- `DOS_BUILD.md` - This file

### Modified Files:
- `main.c` - Conditional inclusion of DOS getopt
- `all.h` - Added `__WATCOMC__` compatibility macro for `__attribute__`

## Compilation Steps

### On your Mac with OpenWatcom installed:

```bash
cd ~/projects/open-watcom-v2/
source mysetvars.sh

# Navigate to qbe directory
cd /path/to/qbe

# Build using Watcom make
wmake -f watcom.mak
```

## Expected Compilation Issues

You will likely encounter several issues:

### 1. C99 Features
OpenWatcom v2 supports most C99 features, but some may need adjustment:
- Designated initializers (`.field = value`) - Should work in v2
- Variadic macros - Should work in v2
- `inline` keyword - May need to use `#pragma inline` instead

### 2. Memory Model Issues
With huge memory model (`-mh`):
- Data structures > 64KB require far pointers
- Stack limited to 32KB (set in linker)
- Total memory limited to 640KB

### 3. Pragma Requirements
You may need to add:
```c
#pragma off (unreferenced)
```
For unused parameter warnings.

### 4. File I/O
DOS file I/O may have issues with:
- Long filenames (8.3 name restriction)
- Path separators (\ vs /)
- Binary vs text mode

## Makefile Options

The `watcom.mak` file uses:
- `-zq` - Quiet mode (reduce output)
- `-w4` - Warning level 4
- `-mh` - Huge memory model
- `-bt=dos` - Build target DOS
- `-DDOS` - Define DOS macro
- `-I.` - Include current directory

## Testing

If compilation succeeds, test with small inputs:

```bash
qbe -t ? # Should print: amd64_sysv
qbe -o test.s test.ssa
```

## Troubleshooting

### Out of Memory Errors
- Reduce input file size
- Use DOS extender (CWSDPMI, DOS4GW) - but would require recompiling for protected mode
- Consider using Watcom's 32-bit compilers instead (wcc386)

### Undefined Symbols
- Check that all object files are linked
- Verify all source files compiled successfully

### Stack Overflow
- Increase stack size in `watcom.mak` (currently 32KB)
- Reduce recursion depth in code

## Alternative: 32-bit Protected Mode DOS

For better results, consider using:
- **wcc386** (32-bit compiler) with DOS/4GW extender
- This removes the 640KB memory limit
- Provides flat 32-bit addressing
- Much faster 64-bit integer operations

To use 32-bit mode, modify `watcom.mak`:
```makefile
CC = wcc386
CFLAGS = -zq -w4 -mf -bt=dos -DDOS -I.
LFLAGS = system dos4g
```

## References

- OpenWatcom v2 Documentation: http://openwatcom.org/
- QBE Documentation: https://c9x.me/compile/
- DOS Memory Models: https://en.wikipedia.org/wiki/Intel_Memory_Model

## Notes

This port is primarily educational. For production use:
1. Use a native 32/64-bit compiler on Linux/macOS/Windows
2. Or use OpenWatcom's 32-bit protected mode
3. Or cross-compile from Linux using standard tools

The 16-bit real mode DOS build is not recommended for serious work due to severe memory constraints.
