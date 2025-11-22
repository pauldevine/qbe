# Hello World for DOS using direct syscall
# Phase 1 - First program with visible output

dos_putchar(int ch) {
	# Implemented in assembly
}

main() {
	# Print "Hi!" using DOS INT 21h AH=02h
	dos_putchar(72);  # 'H'
	dos_putchar(105); # 'i'
	dos_putchar(33);  # '!'
	dos_putchar(13);  # '\r'
	dos_putchar(10);  # '\n'
	return 0;
}
