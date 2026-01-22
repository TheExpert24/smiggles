void kernel_main(void) {
    char* video = (char*)0xB8000;
    int cursor = 0;
    int prompt_end = 0;
    unsigned char prev_scancode = 0;
    
    // Clear screen + welcome
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }
    const char* msg = "Smiggles\n> ";
    int i = 0;
    while (msg[i]) {
        if (msg[i] == '\n') {
            cursor = ((cursor / 80) + 1) * 80;
        } else {
            video[cursor*2] = msg[i];
            video[cursor*2+1] = 0x0F;
            cursor++;
        }
        i++;
    }
    prompt_end = cursor;

    // Set hardware cursor to match logical cursor
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
    
    while (1) {
        unsigned char scancode;
        asm volatile("inb $0x60, %0" : "=a"(scancode));
        
        // Key release resets filter
        if (scancode > 0x80) {
            prev_scancode = 0;
        }
        // New key press only
        else if (scancode != prev_scancode && scancode != 0) {
            prev_scancode = scancode;
            
            char c = 0;
            switch (scancode) {
                case 0x02: c = '1'; break; case 0x03: c = '2'; break;
                case 0x04: c = '3'; break; case 0x05: c = '4'; break;
                case 0x06: c = '5'; break; case 0x07: c = '6'; break;
                case 0x08: c = '7'; break; case 0x09: c = '8'; break;
                case 0x0A: c = '9'; break; case 0x0B: c = '0'; break;
                case 0x10: c = 'q'; break; case 0x11: c = 'w'; break;
                case 0x12: c = 'e'; break; case 0x13: c = 'r'; break;
                case 0x14: c = 't'; break; case 0x15: c = 'y'; break;
                case 0x16: c = 'u'; break; case 0x17: c = 'i'; break;
                case 0x18: c = 'o'; break; case 0x19: c = 'p'; break;
                case 0x1E: c = 'a'; break; case 0x1F: c = 's'; break;
                case 0x20: c = 'd'; break; case 0x21: c = 'f'; break;
                case 0x22: c = 'g'; break; case 0x23: c = 'h'; break;
                case 0x24: c = 'j'; break; case 0x25: c = 'k'; break;
                case 0x26: c = 'l'; break; case 0x2C: c = 'z'; break;
                case 0x2D: c = 'x'; break; case 0x2E: c = 'c'; break;
                case 0x2F: c = 'v'; break; case 0x30: c = 'b'; break;
                case 0x31: c = 'n'; break; case 0x32: c = 'm'; break;
                case 0x39: c = ' '; break; case 0x1C: c = '\n'; break;
                case 0x0E: c = 8; break;  // backspace
            }
            
            if (c) {
                if (c == '\n') {
                    cursor = ((cursor / 80) + 1) * 80;
                } 
                else if (c == 8 && cursor > prompt_end) {
                    cursor--;
                    video[cursor*2] = ' ';
                    video[cursor*2+1] = 0x07;
                }
                else if (cursor < 80*25 - 1) {
                    video[cursor*2] = c;
                    video[cursor*2+1] = 0x0F;
                    cursor++;
                }
                // Update hardware cursor after every change
                unsigned short pos = cursor;
                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
            }
        }
    }
}
