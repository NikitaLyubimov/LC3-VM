#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<unistd.h>
#include<termios.h>
#include<sys/types.h>
#include<sys/select.h>

#define PC_START 0x3000

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_PSR,
    R_COUNT
};

enum
{
    OP_BR = 0,
    OP_ADD,
    OP_LD,
    OP_ST,
    OP_JSR,
    OP_AND,
    OP_LDR,
    OP_STR,
    OP_RTI,
    OP_NOT,
    OP_LDI,
    OP_STI,
    OP_JMP,
    OP_XOR,
    OP_LEA,
    OP_TRAP
};

enum
{
    FL_POS = 1 << 0,
    FL_NEG = 1 << 2,
    FL_ZERO = 1 << 1
};

enum
{
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

enum
{
    MR_KBSR = 0xFE00,
    MR_KBDR = 0xFE02
};

uint16_t memory[UINT16_MAX];
uint16_t registers[R_COUNT];


uint16_t swap_16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    uint16_t origin;

    fread(&origin, sizeof(uint16_t), 1, file);
    origin = swap_16(origin);

    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;

    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    while (read-- > 0)
    {
        *p = swap_16(*p);
        ++p;
    }
    

}

int read_image(const char* path)
{
    FILE* file = fopen(path, "rb");

    read_image_file(file);
    fclose(file);
    return 1;
}

void memory_write(uint16_t address, uint16_t value)
{
    memory[address] = value;
}

uint16_t sign_extended(uint16_t digit, int bit_count)
{
    if((digit >> bit_count) & 1)
    {
        digit |= (0xFFFF << bit_count);
    }

    return digit;
}

uint16_t check_key()
{
    fd_set readfs;
    FD_ZERO(&readfs);
    FD_SET(STDIN_FILENO, &readfs);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfs, NULL, NULL, &timeout) != 0;
}

struct termios origin;
void disable_buffering()
{
    tcgetattr(STDIN_FILENO, &origin);
    struct termios new_origin = origin;

    origin.c_lflag &= (~ICANON);
    origin.c_lflag &= (~ECHO);
    
    tcsetattr(STDIN_FILENO, TCSANOW, &new_origin);
}

void enable_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origin);
}

uint16_t memory_read(uint16_t address)
{
    if(address == MR_KBSR)
    {
        if(check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
            memory[MR_KBSR] = 0;
    }

    return memory[address];
}

void update_flags(uint16_t address)
{
    if(registers[address] == 0)
        registers[R_COND] = FL_ZERO;
    else if(registers[address] >> 15)
        registers[R_COND] = FL_NEG;
    else
        registers[R_COND] = FL_POS;
        
}

void handle_interrupt(int signal)
{
    enable_buffering();
    printf("\n");
    exit(-2);
}


int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printf("usage: program [filename]\n");
        exit(-1);
    }

    for(int i = 1;i < argc;++i)
    {
        if(!read_image(argv[1]))
        {
            printf("failed to load image");
            exit(-1);
        }
    }

    registers[R_PC] = PC_START;
    signal(SIGINT, handle_interrupt);

    disable_buffering();

    registers[R_PC] = PC_START;

    int running = 1;

    while (running)
    {
        uint16_t instr = memory_read(registers[R_PC]++);
        printf("PC: %d\n", (int)registers[R_PC]);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t sr1 = (instr >> 6) & 0x7;

                uint16_t flag = (instr >> 5) & 0x1;
                uint16_t imm5;
                uint16_t sr2;

                if(flag)
                {
                    imm5 = sign_extended(instr & 0x1F, 5);
                    registers[dr] = registers[sr1] + imm5;
                }
                    
                else
                {
                    sr2 = instr & 0x7;
                    registers[dr] = registers[sr1] + registers[sr2];
                }
                
                update_flags(dr);
            }
            break;
        
            case OP_AND:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t sr1 = (instr >> 6) & 0x7;

                uint16_t flag = (instr >> 5) & 0x1;

                if(flag)
                {
                    uint16_t imm5 = sign_extended(instr & 0x1F, 5);
                    registers[dr] = registers[sr1] & imm5;
                }
                else
                {
                    uint16_t sr2 = instr & 0x7;
                    registers[dr] = registers[sr1] & registers[sr2];
                }

                update_flags(dr);
            }
            break;

            case OP_BR:
            {
                uint16_t pc_offset = sign_extended(instr & 0x1FF, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;

                if(cond_flag & registers[R_COND])
                {
                    registers[R_PC] += pc_offset;
                }
            }
            break;

            case OP_JMP:
            {
                uint16_t r1 = (instr >> 6) & 0x7;
                registers[R_PC] = registers[r1];
            }
            break;

            case OP_JSR:
            {
                registers[R_R7] = registers[R_PC];

                uint16_t flag = (instr >> 11) & 0x1;

                if(flag)
                {
                    uint16_t pc_offset = sign_extended(instr & 0x7FF, 11);
                    registers[R_PC] += pc_offset;
                }
                else
                {
                    uint16_t base_r = (instr >> 6) & 0x7;
                    registers[R_PC] = registers[base_r];
                }
            }
            break;
        
            case OP_LD:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extended(instr & 0X1FF, 9);

                registers[dr] = memory_read(registers[R_PC] + pc_offset);
                update_flags(dr);
            }
            break;

            case OP_LDI:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extended(instr & 0x1FF, 9);

                registers[dr] = memory_read(memory_read(registers[R_PC] + pc_offset));
                update_flags(dr);
            }
            break;

            case OP_LDR:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t base_r = (instr >> 6) & 0x7;
                uint16_t offset = sign_extended(instr & 0x3F, 6);

                registers[dr] = memory_read(registers[base_r] + offset);
                update_flags(dr);
            }
            break;

            case OP_LEA:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extended(instr & 0x1FF, 9);

                registers[dr] = registers[R_PC] + pc_offset;
                update_flags(dr);
            }
            break;
        
            case OP_NOT:
            {
                uint16_t dr =(instr >> 9) & 0x7;
                uint16_t sr = (instr >> 6) & 0x7;

                registers[dr] = ~sr;
                update_flags(dr);
            }
            break;

        
            case OP_ST:
            {
                uint16_t st = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extended(instr & 0x1FF, 9);
                memory_write(registers[R_PC] + pc_offset, registers[st]);
            }
            break;
            case OP_STI:
            {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extended(instr & 0x1FF, 9);
                memory_write(memory_read(registers[R_PC] + pc_offset), registers[sr]);
            }
            break;
            case OP_STR:
            {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t base_r = (instr >> 6) & 0x7;
                uint16_t offset = sign_extended(instr & 0x3F, 6);
                memory_write(registers[base_r] + offset, registers[sr]);
            }
            break;
            case OP_TRAP:
            {
                switch(instr & 0xFF)
                {
                    case TRAP_GETC:
                        {
                            registers[R_R0] = (uint16_t)getchar();
                        }
                        break;
                    case TRAP_OUT:
                        {
                            char ch = (char)registers[R_R0];
                            putc(ch, stdout);
                            fflush(stdout);
                        }
                        break;
                    case TRAP_PUTS:
                        {
                            uint16_t address = registers[R_R0];
                            uint16_t* string = memory + address;

                            while (*string)
                            {
                                putc((char)*string, stdout);
                                ++string;
                            }
                            fflush(stdout);
                            
                        }
                        break;
                    case TRAP_IN:
                        {
                            printf("Enter character\n");
                            char ch = getchar();
                            putc(ch, stdout);
                            registers[R_R0] = (uint16_t)ch;

                        }
                        break;
                    case TRAP_PUTSP:
                        {
                            uint16_t* string = memory + registers[R_R0];
                            while(*string)
                            {
                                char char1 = (*string) & 0xFF;
                                putc(char1, stdout);
                                char char2 = (*string) >> 8;
                                if(char2) putc(char2, stdout);
                                ++string;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        {
                            puts("HALT");
                            fflush(stdout);
                            running = 0;
                        }
                        break;

                }
            }
            break;
        case OP_XOR:
            {
                uint16_t r = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;

                registers[r] = registers[r]^registers[r1];
                update_flags(r);    
            }
            break;

        case OP_RTI:
        printf("RTIII PERFORM\n");
        
        default:
            abort();
            break;
        }

    }

    enable_buffering();
    
}