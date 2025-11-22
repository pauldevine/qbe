# Standard C Library Subset for DOS
# Implements essential C library functions using DOS API

# ============================================================================
# String Functions
# ============================================================================

strlen(char *s) {
    int len;
    len = 0;
    while (s[len]) {
        len = len + 1;
    }
    return len;
}

strcpy(char *dest, char *src) {
    int i;
    i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i = i + 1;
    }
    dest[i] = 0;
    return dest;
}

strcat(char *dest, char *src) {
    int i, j;
    i = 0;
    while (dest[i]) {
        i = i + 1;
    }
    j = 0;
    while (src[j]) {
        dest[i] = src[j];
        i = i + 1;
        j = j + 1;
    }
    dest[i] = 0;
    return dest;
}

strcmp(char *s1, char *s2) {
    int i;
    i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
        i = i + 1;
    }
    return s1[i] - s2[i];
}

# ============================================================================
# Memory Functions
# ============================================================================

memcpy(char *dest, char *src, int n) {
    int i;
    i = 0;
    while (i < n) {
        dest[i] = src[i];
        i = i + 1;
    }
    return dest;
}

memset(char *s, int c, int n) {
    int i;
    i = 0;
    while (i < n) {
        s[i] = c;
        i = i + 1;
    }
    return s;
}

memcmp(char *s1, char *s2, int n) {
    int i;
    i = 0;
    while (i < n) {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
        i = i + 1;
    }
    return 0;
}

# ============================================================================
# Character Classification
# ============================================================================

isalpha(int c) {
    if (c >= 65 && c <= 90) return 1;   # A-Z
    if (c >= 97 && c <= 122) return 1;  # a-z
    return 0;
}

isdigit(int c) {
    if (c >= 48 && c <= 57) return 1;   # 0-9
    return 0;
}

isspace(int c) {
    if (c == 32) return 1;   # space
    if (c == 9) return 1;    # tab
    if (c == 10) return 1;   # newline
    if (c == 13) return 1;   # carriage return
    return 0;
}

isalnum(int c) {
    if (isalpha(c)) return 1;
    if (isdigit(c)) return 1;
    return 0;
}

toupper(int c) {
    if (c >= 97 && c <= 122) {  # a-z
        return c - 32;
    }
    return c;
}

tolower(int c) {
    if (c >= 65 && c <= 90) {  # A-Z
        return c + 32;
    }
    return c;
}

# ============================================================================
# Numeric Functions
# ============================================================================

abs(int x) {
    if (x < 0) {
        return -x;
    }
    return x;
}

# Convert integer to string
itoa(int value, char *str, int base) {
    int i, sign, digit;
    char digits[16];

    # Handle negative numbers for base 10
    sign = 0;
    if (value < 0 && base == 10) {
        sign = 1;
        value = -value;
    }

    # Generate digits in reverse order
    i = 0;
    while (value > 0) {
        digit = value % base;
        if (digit < 10) {
            digits[i] = 48 + digit;  # '0' + digit
        } else {
            digits[i] = 65 + (digit - 10);  # 'A' + (digit - 10)
        }
        value = value / base;
        i = i + 1;
    }

    # Handle zero special case
    if (i == 0) {
        digits[0] = 48;  # '0'
        i = 1;
    }

    # Add negative sign if needed
    if (sign) {
        digits[i] = 45;  # '-'
        i = i + 1;
    }

    # Reverse the string
    {
        int j, k;
        j = 0;
        k = i - 1;
        while (k >= 0) {
            str[j] = digits[k];
            j = j + 1;
            k = k - 1;
        }
        str[j] = 0;
    }

    return str;
}

# Convert string to integer
atoi(char *str) {
    int result, sign, i;

    result = 0;
    sign = 1;
    i = 0;

    # Skip whitespace
    while (isspace(str[i])) {
        i = i + 1;
    }

    # Check for sign
    if (str[i] == 45) {  # '-'
        sign = -1;
        i = i + 1;
    } else if (str[i] == 43) {  # '+'
        i = i + 1;
    }

    # Convert digits
    while (isdigit(str[i])) {
        result = result * 10 + (str[i] - 48);  # str[i] - '0'
        i = i + 1;
    }

    return result * sign;
}

# ============================================================================
# Simple I/O Functions
# ============================================================================

putchar(int c) {
    dos_putchar(c);
    return c;
}

puts(char *s) {
    dos_puts(s);
    dos_putchar(13);  # CR
    dos_putchar(10);  # LF
    return 0;
}

getchar() {
    return dos_getchar();
}

# ============================================================================
# Simple printf (integers and strings only)
# ============================================================================

# This is a very basic printf that supports:
# %d - decimal integer
# %x - hexadecimal integer
# %s - string
# %c - character
# %% - percent sign

printf_internal(char *fmt, int arg1, int arg2, int arg3, int arg4) {
    int i, argidx;
    char temp[32];

    i = 0;
    argidx = 0;

    while (fmt[i]) {
        if (fmt[i] == 37) {  # '%'
            i = i + 1;
            if (fmt[i] == 100) {  # 'd' - decimal
                if (argidx == 0) itoa(arg1, temp, 10);
                else if (argidx == 1) itoa(arg2, temp, 10);
                else if (argidx == 2) itoa(arg3, temp, 10);
                else if (argidx == 3) itoa(arg4, temp, 10);
                dos_puts(temp);
                argidx = argidx + 1;
            } else if (fmt[i] == 120) {  # 'x' - hex
                if (argidx == 0) itoa(arg1, temp, 16);
                else if (argidx == 1) itoa(arg2, temp, 16);
                else if (argidx == 2) itoa(arg3, temp, 16);
                else if (argidx == 3) itoa(arg4, temp, 16);
                dos_puts(temp);
                argidx = argidx + 1;
            } else if (fmt[i] == 115) {  # 's' - string
                if (argidx == 0) dos_puts(arg1);
                else if (argidx == 1) dos_puts(arg2);
                else if (argidx == 2) dos_puts(arg3);
                else if (argidx == 3) dos_puts(arg4);
                argidx = argidx + 1;
            } else if (fmt[i] == 99) {  # 'c' - character
                if (argidx == 0) dos_putchar(arg1);
                else if (argidx == 1) dos_putchar(arg2);
                else if (argidx == 2) dos_putchar(arg3);
                else if (argidx == 3) dos_putchar(arg4);
                argidx = argidx + 1;
            } else if (fmt[i] == 37) {  # '%' - percent
                dos_putchar(37);
            }
            i = i + 1;
        } else {
            dos_putchar(fmt[i]);
            i = i + 1;
        }
    }
    return 0;
}
