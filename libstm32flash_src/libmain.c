/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright 2010 Geoffrey McRae <geoff@spacevs.com>
  Copyright 2011 Steve Markgraf <steve@steve-m.de>
  Copyright 2012-2016 Tormod Volden <debian.tormod@gmail.com>
  Copyright 2013-2016 Antonio Borneo <borneo.antonio@gmail.com>
  Copyright 2021 Renaud Fivet <renaud.fivet@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <signal.h>

#include "init.h"
#include "utils.h"
#include "serial.h"
#include "stm32.h"
#include "parsers/parser.h"
#include "port.h"

#include "parsers/binary.h"
#include "parsers/hex.h"

#if defined(__WIN32__) || defined(__CYGWIN__)
#include <windows.h>
#include "getopt/getopt.h"
#else
#include <unistd.h>
#endif

#define VERSION "0.6"

/* device globals */
stm32_t		*stm		= NULL;
void		*p_st		= NULL;
parser_t	*parser		= NULL;
struct port_interface *port = NULL;

/* settings */
struct port_options port_opts = {
    .device			= NULL,
    .baudRate		= SERIAL_BAUD_57600,
    .serial_mode		= "8e1",
    .bus_addr		= 0,
    .rx_frame_max		= STM32_MAX_RX_FRAME,
    .tx_frame_max		= STM32_MAX_TX_FRAME,
};

enum actions {
    ACT_NONE,
    ACT_READ,
    ACT_WRITE,
    ACT_WRITE_UNPROTECT,
    ACT_READ_PROTECT,
    ACT_READ_UNPROTECT,
    ACT_ERASE_ONLY,
    ACT_CRC
};

enum actions	action		= ACT_NONE;
int		npages		= 0;
int             spage           = 0;
int             no_erase        = 0;
char		verify		= 0;
int		retry		= 10;
char		exec_flag	= 0;
uint32_t	execute		= 0;
char		init_flag	= 1;
int		use_stdinout	= 0;
char		force_binary	= 0;
FILE		*diag;
char		reset_flag	= 0;
char		*filename;
char		*gpio_seq	= NULL;
uint32_t	start_addr	= 0;
uint32_t	readwrite_len	= 0;

//extern "C"
#ifdef __WIN32__
#define DLL_EXPORT(ret_type)   __declspec(dllexport)  ret_type __stdcall
#else
#define DLL_EXPORT(ret_type) ret_type
#endif









/* functions */
int  parse_options(char *prog_name);
DLL_EXPORT(void) show_help();

static const char *action2str(enum actions act)
{
    switch (act) {
    case ACT_READ:
        return "memory read";
    case ACT_WRITE:
        return "memory write";
    case ACT_WRITE_UNPROTECT:
        return "write unprotect";
    case ACT_READ_PROTECT:
        return "read protect";
    case ACT_READ_UNPROTECT:
        return "read unprotect";
    case ACT_ERASE_ONLY:
        return "flash erase";
    case ACT_CRC:
        return "memory crc";
    default:
        return "";
    };
}

static void err_multi_action(enum actions new)
{
    fprintf(stderr,
            "ERROR: Invalid options !\n"
            "\tCan't execute \"%s\" and \"%s\" at the same time.\n",
            action2str(action), action2str(new));
}

static int is_addr_in_ram(uint32_t addr)
{
    return addr >= stm->dev->ram_start && addr < stm->dev->ram_end;
}

static int is_addr_in_flash(uint32_t addr)
{
    return addr >= stm->dev->fl_start && addr < stm->dev->fl_end;
}

static int is_addr_in_opt_bytes(uint32_t addr)
{
    /* option bytes upper range is inclusive in our device table */
    return addr >= stm->dev->opt_start && addr <= stm->dev->opt_end;
}

static int is_addr_in_sysmem(uint32_t addr)
{
    return addr >= stm->dev->mem_start && addr < stm->dev->mem_end;
}

/* returns the page that contains address "addr" */
static int flash_addr_to_page_floor(uint32_t addr)
{
    int page;
    uint32_t *psize;

    if (!is_addr_in_flash(addr))
        return 0;

    page = 0;
    addr -= stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    while (addr >= psize[0]) {
        addr -= psize[0];
        page++;
        if (psize[1])
            psize++;
    }

    return page;
}

/* returns the first page whose start addr is >= "addr" */
int flash_addr_to_page_ceil(uint32_t addr)
{
    int page;
    uint32_t *psize;

    if (!(addr >= stm->dev->fl_start && addr <= stm->dev->fl_end))
        return 0;

    page = 0;
    addr -= stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    while (addr >= psize[0]) {
        addr -= psize[0];
        page++;
        if (psize[1])
            psize++;
    }

    return addr ? page + 1 : page;
}

/* returns the lower address of flash page "page" */
static uint32_t flash_page_to_addr(int page)
{
    int i;
    uint32_t addr, *psize;

    addr = stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    for (i = 0; i < page; i++) {
        addr += psize[0];
        if (psize[1])
            psize++;
    }

    return addr;
}


#if defined(__WIN32__) || defined(__CYGWIN__)
BOOL CtrlHandler( DWORD fdwCtrlType )
{
    fprintf(stderr, "\nCaught signal %lu\n",fdwCtrlType);
    if (p_st &&  parser ) parser->close(p_st);
    if (stm  ) stm32_close  (stm);
    if (port) port->close(port);
    exit(1);
}
#else
void sighandler(int s){
    fprintf(stderr, "\nCaught signal %d\n",s);
    if (p_st &&  parser ) parser->close(p_st);
    if (stm  ) stm32_close  (stm);
    if (port) port->close(port);
    exit(1);
}
#endif



DLL_EXPORT(int) reset_params()
{
    port_opts.device= NULL;
    port_opts.baudRate		= SERIAL_BAUD_57600;
    port_opts.serial_mode		= "8e1";
    port_opts.bus_addr		= 0;
    port_opts.rx_frame_max		= STM32_MAX_RX_FRAME;
    port_opts.tx_frame_max		= STM32_MAX_TX_FRAME;

    action		= ACT_NONE;
    npages		= 0;
    spage           = 0;
    no_erase        = 0;
    verify		= 0;
    retry		= 10;
    exec_flag	= 0;
    execute		= 0;
    init_flag	= 1;
    use_stdinout	= 0;
    force_binary	= 0;
    diag=NULL;
    reset_flag	= 0;
    filename=NULL;
    gpio_seq	= NULL;
    start_addr	= 0;
    readwrite_len	= 0;
    return 0;
}


DLL_EXPORT(int) run_it() {
    int ret = 1;
    stm32_err_t s_err;
    parser_err_t perr;
    diag = stdout;
    char prog_name[]="stm32flash";

    if (parse_options(prog_name) != 0)
        goto close;

    if (action == ACT_READ && use_stdinout) {
        diag = stderr;
    }

    fprintf(diag, "stm32flash " VERSION "\n\n");
    fprintf(diag, "http://stm32flash.sourceforge.net/\n\n");

#if defined(__WIN32__) || defined(__CYGWIN__)
    SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE );
#else
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = sighandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
#endif

    if (action == ACT_WRITE) {
        /* first try hex */
        if (!force_binary) {
            parser = &PARSER_HEX;
            p_st = parser->init();
            if (!p_st) {
                fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                goto close;
            }
        }

        if (force_binary || (perr = parser->open(p_st, filename, 0)) != PARSER_ERR_OK) {
            if (force_binary || perr == PARSER_ERR_INVALID_FILE) {
                if (!force_binary) {
                    parser->close(p_st);
                    p_st = NULL;
                }

                /* now try binary */
                parser = &PARSER_BINARY;
                p_st = parser->init();
                if (!p_st) {
                    fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                    goto close;
                }
                perr = parser->open(p_st, filename, 0);
            }

            /* if still have an error, fail */
            if (perr != PARSER_ERR_OK) {
                fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
                if (perr == PARSER_ERR_SYSTEM) perror(filename);
                goto close;
            }
        }

        fprintf(diag, "Using Parser : %s\n", parser->name);

        /* We may know from the file how much data there is */
        if (!use_stdinout) {
            if (!start_addr)
                start_addr = parser->base(p_st);
            if (start_addr)
                fprintf(diag, "Location     : %#08x\n", start_addr);

            if (!readwrite_len)
                readwrite_len = parser->size(p_st);
            fprintf(diag, "Size         : %u\n", readwrite_len);
        }
    } else {
        parser = &PARSER_BINARY;
        p_st = parser->init();
        if (!p_st) {
            fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
            goto close;
        }
    }

    if (port_open(&port_opts, &port) != PORT_ERR_OK) {
        fprintf(stderr, "Failed to open port: %s\n", port_opts.device);
        goto close;
    }

    fprintf(diag, "Interface %s: %s\n", port->name, port->get_cfg_str(port));
    if (init_flag && init_bl_entry(port, gpio_seq)){
        ret = 1;
        fprintf(stderr, "Failed to send boot enter sequence\n");
        goto close;
    }

    port->flush(port);

    stm = stm32_init(port, init_flag);
    if (!stm)
        goto close;

    fprintf(diag, "Version      : 0x%02x\n", stm->bl_version);
    if (port->flags & PORT_GVR_ETX) {
        fprintf(diag, "Option 1     : 0x%02x\n", stm->option1);
        fprintf(diag, "Option 2     : 0x%02x\n", stm->option2);
    }
    fprintf(diag, "Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
    fprintf(diag, "- RAM        : Up to %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
    fprintf(diag, "- Flash      : Up to %dKiB (size first sector: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps[0]);
    fprintf(diag, "- Option RAM : %db\n", stm->dev->opt_end - stm->dev->opt_start + 1);
    fprintf(diag, "- System RAM : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);

    uint8_t		buffer[256];
    uint32_t	addr, start, end;
    unsigned int	len;
    int		failed = 0;
    int		first_page, num_pages;

    /*
     * Cleanup addresses:
     *
     * Starting from options
     *	start_addr, readwrite_len, spage, npages
     * and using device memory size, compute
     *	start, end, first_page, num_pages
     */
    if (start_addr || readwrite_len) {
        if (start_addr == 0)
            /* default */
            start = stm->dev->fl_start;
        else if (start_addr == 1)
            /* if specified to be 0 by user */
            start = 0;
        else
            start = start_addr;

        if (is_addr_in_flash(start))
            end = stm->dev->fl_end;
        else {
            no_erase = 1;
            if (is_addr_in_ram(start))
                end = stm->dev->ram_end;
            else if (is_addr_in_opt_bytes(start))
                end = stm->dev->opt_end + 1;
            else if (is_addr_in_sysmem(start))
                end = stm->dev->mem_end;
            else {
                /* Unknown territory */
                if (readwrite_len)
                    end = start + readwrite_len;
                else
                    end = start + sizeof(uint32_t);
            }
        }

        if (readwrite_len && (end > start + readwrite_len))
            end = start + readwrite_len;

        first_page = flash_addr_to_page_floor(start);
        if (!first_page && end == stm->dev->fl_end)
            num_pages = STM32_MASS_ERASE;
        else
            num_pages = flash_addr_to_page_ceil(end) - first_page;
    } else if (!spage && !npages) {
        start = stm->dev->fl_start;
        end = stm->dev->fl_end;
        first_page = 0;
        num_pages = STM32_MASS_ERASE;
    } else {
        first_page = spage;
        start = flash_page_to_addr(first_page);
        if (start > stm->dev->fl_end) {
            fprintf(stderr, "Address range exceeds flash size.\n");
            goto close;
        }

        if (npages) {
            num_pages = npages;
            end = flash_page_to_addr(first_page + num_pages);
            if (end > stm->dev->fl_end)
                end = stm->dev->fl_end;
        } else {
            end = stm->dev->fl_end;
            num_pages = flash_addr_to_page_ceil(end) - first_page;
        }

        if (!first_page && end == stm->dev->fl_end)
            num_pages = STM32_MASS_ERASE;
    }

    if (action == ACT_READ) {
        unsigned int max_len = port_opts.rx_frame_max;

        fprintf(diag, "Memory read\n");

        perr = parser->open(p_st, filename, 1);
        if (perr != PARSER_ERR_OK) {
            fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
            if (perr == PARSER_ERR_SYSTEM)
                perror(filename);
            goto close;
        }

        fflush(diag);
        addr = start;
        while(addr < end) {
            uint32_t left	= end - addr;
            len		= max_len > left ? left : max_len;
            s_err = stm32_read_memory(stm, addr, buffer, len);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to read memory at address 0x%08x, target write-protected?\n", addr);
                goto close;
            }
            if (parser->write(p_st, buffer, len) != PARSER_ERR_OK)
            {
                fprintf(stderr, "Failed to write data to file\n");
                goto close;
            }
            addr += len;

            fprintf(diag,
                    "\rRead address 0x%08x (%.2f%%) ",
                    addr,
                    (100.0f / (float)(end - start)) * (float)(addr - start)
                    );
            fflush(diag);
        }
        fprintf(diag,	"Done.\n");
        ret = 0;
        goto close;
    } else if (action == ACT_READ_PROTECT) {
        fprintf(diag, "Read-Protecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_readprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read-protect flash\n");
            goto close;
        }
        fprintf(diag,	"Done.\n");
        ret = 0;
    } else if (action == ACT_READ_UNPROTECT) {
        fprintf(diag, "Read-UnProtecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_runprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read-unprotect flash\n");
            goto close;
        }
        fprintf(diag,	"Done.\n");
        ret = 0;
    } else if (action == ACT_ERASE_ONLY) {
        ret = 0;
        fprintf(diag, "Erasing flash\n");

        if (num_pages != STM32_MASS_ERASE &&
                (start != flash_page_to_addr(first_page)
                 || end != flash_page_to_addr(first_page + num_pages))) {
            fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
            ret = 1;
            goto close;
        }

        s_err = stm32_erase_memory(stm, first_page, num_pages);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to erase memory\n");
            ret = 1;
            goto close;
        }
        ret = 0;
    } else if (action == ACT_WRITE_UNPROTECT) {
        fprintf(diag, "Write-unprotecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_wunprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to write-unprotect flash\n");
            goto close;
        }
        fprintf(diag,	"Done.\n");
        ret = 0;
    } else if (action == ACT_WRITE) {
        fprintf(diag, "Write to memory\n");

        unsigned int offset = 0;
        unsigned int r;
        unsigned int size;
        unsigned int max_wlen, max_rlen;

        max_wlen = port_opts.tx_frame_max - 2;	/* skip len and crc */
        max_wlen &= ~3;	/* 32 bit aligned */

        max_rlen = port_opts.rx_frame_max;
        max_rlen = max_rlen < max_wlen ? max_rlen : max_wlen;

        /* Assume data from stdin is whole device */
        if (use_stdinout)
            size = end - start;
        else
            size = parser->size(p_st);

        // TODO: It is possible to write to non-page boundaries, by reading out flash
        //       from partial pages and combining with the input data
        // if ((start % stm->dev->fl_ps[i]) != 0 || (end % stm->dev->fl_ps[i]) != 0) {
        //	fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
        //	goto close;
        // }

        // TODO: If writes are not page aligned, we should probably read out existing flash
        //       contents first, so it can be preserved and combined with new data
        if (!no_erase && num_pages) {
            fprintf(diag, "Erasing memory\n");
            s_err = stm32_erase_memory(stm, first_page, num_pages);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to erase memory\n");
                goto close;
            }
        }

        fflush(diag);
        addr = start;
        while(addr < end && offset < size) {
            uint32_t left	= end - addr;
            len		= max_wlen > left ? left : max_wlen;
            len		= len > size - offset ? size - offset : len;
            unsigned int reqlen = len ;

            if (parser->read(p_st, buffer, &len) != PARSER_ERR_OK)
                goto close;

            if (len == 0) {
                if (use_stdinout) {
                    break;
                } else {
                    fprintf(stderr, "Failed to read input file\n");
                    goto close;
                }
            }

again:
            s_err = stm32_write_memory(stm, addr, buffer, len);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to write memory at address 0x%08x\n", addr);
                goto close;
            }

            if (verify) {
                uint8_t *compare= (uint8_t*)malloc(len);
                unsigned int offset, rlen;

                offset = 0;
                while (offset < len) {
                    rlen = len - offset;
                    rlen = rlen < max_rlen ? rlen : max_rlen;
                    s_err = stm32_read_memory(stm, addr + offset, compare + offset, rlen);
                    if (s_err != STM32_ERR_OK) {
                        fprintf(stderr, "Failed to read memory at address 0x%08x\n", addr + offset);
                        free(compare);
                        goto close;
                    }
                    offset += rlen;
                }

                for(r = 0; r < len; ++r)
                    if (buffer[r] != compare[r]) {
                        if (failed == retry) {
                            fprintf(stderr, "Failed to verify at address 0x%08x, expected 0x%02x and found 0x%02x\n",
                                    (uint32_t)(addr + r),
                                    buffer [r],
                                    compare[r]
                                    );
                            free(compare);
                            goto close;
                        }
                        ++failed;
                        free(compare);
                        goto again;
                    }

                failed = 0;
                free(compare);
            }

            addr	+= len;
            offset	+= len;

            fprintf(diag,
                    "\rWrote %saddress 0x%08x (%.2f%%) ",
                    verify ? "and verified " : "",
                    addr,
                    (100.0f / size) * offset
                    );
            fflush(diag);

            if( len < reqlen)	/* Last read already reached EOF */
                break ;
        }

        fprintf(diag,	"Done.\n");
        ret = 0;
        goto close;
    } else if (action == ACT_CRC) {
        uint32_t crc_val = 0;

        fprintf(diag, "CRC computation\n");

        s_err = stm32_crc_wrapper(stm, start, end - start, &crc_val);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read CRC\n");
            goto close;
        }
        fprintf(diag, "CRC(0x%08x-0x%08x) = 0x%08x\n", start, end,
                crc_val);
        ret = 0;
        goto close;
    } else
        ret = 0;

close:
    if (stm && exec_flag && ret == 0) {
        if (execute == 0)
            execute = stm->dev->fl_start;

        fprintf(diag, "\nStarting execution at address 0x%08x... ", execute);
        fflush(diag);
        if (stm32_go(stm, execute) == STM32_ERR_OK) {
            reset_flag = 0;
            fprintf(diag, "done.\n");
        } else
            fprintf(diag, "failed.\n");
    }

    if (stm && reset_flag) {
        fprintf(diag, "\nResetting device... \n");
        fflush(diag);
        if (init_bl_exit(stm, port, gpio_seq)) {
            ret = 1;
            fprintf(diag, "Reset failed.\n");
        } else
            fprintf(diag, "Reset done.\n");
    } else if (port) {
        /* Always run exit sequence if present */
        if (gpio_seq && strchr(gpio_seq, ':'))
            ret = gpio_bl_exit(port, gpio_seq) || ret;
    }

    if (p_st  ) parser->close(p_st);
    if (stm   ) stm32_close  (stm);
    if (port)
        port->close(port);

    fprintf(diag, "\n");
    return ret;
}



DLL_EXPORT(int) set_arg(char * arg_key, char *arg_val)
{
    int c;
    char *pLen;

    int argc=1;
    char argv[3][300];
    strcpy(argv[0], arg_key);
    if(strlen(arg_val)>0)
    {
        strcpy(argv[1], arg_val);
        argc=2;
    }

    argv[2][0]=0;
    argv[2][1]=0;


    while ((c = getopt(argc, (char**)argv, "a:b:m:r:w:e:vn:g:jkfcChuos:S:F:i:R")) != -1) {
        switch(c) {
        case 'a':
            port_opts.bus_addr = strtoul(optarg, NULL, 0);
            break;

        case 'b':
            port_opts.baudRate = serial_get_baud(strtoul(optarg, NULL, 0));
            if (port_opts.baudRate == SERIAL_BAUD_INVALID) {
                serial_baud_t baudrate;
                fprintf(stderr,	"Invalid baud rate, valid options are:\n");
                for (baudrate = SERIAL_BAUD_1200; baudrate != SERIAL_BAUD_INVALID; ++baudrate)
                    fprintf(stderr, " %d\n", serial_get_baud_int(baudrate));
                return 1;
            }
            break;

        case 'm':
            if (strlen(optarg) != 3
                    || serial_get_bits(optarg) == SERIAL_BITS_INVALID
                    || serial_get_parity(optarg) == SERIAL_PARITY_INVALID
                    || serial_get_stopbit(optarg) == SERIAL_STOPBIT_INVALID) {
                fprintf(stderr, "Invalid serial mode\n");
                return 1;
            }
            port_opts.serial_mode = optarg;
            break;

        case 'r':
        case 'w':
            if (action != ACT_NONE) {
                err_multi_action((c == 'r') ? ACT_READ : ACT_WRITE);
                return 1;
            }
            action = (c == 'r') ? ACT_READ : ACT_WRITE;
            filename = optarg;
            if (filename[0] == '-' && filename[1] == '\0') {
                use_stdinout = 1;
                force_binary = 1;
            }
            break;
        case 'e':
            if (readwrite_len || start_addr) {
                fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
                return 1;
            }
            npages = strtoul(optarg, NULL, 0);
            if (npages > STM32_MAX_PAGES || npages < 0) {
                fprintf(stderr, "ERROR: You need to specify a page count between 0 and 0xffff");
                return 1;
            }
            if (!npages)
                no_erase = 1;
            break;
        case 'u':
            if (action != ACT_NONE) {
                err_multi_action(ACT_WRITE_UNPROTECT);
                return 1;
            }
            action = ACT_WRITE_UNPROTECT;
            break;

        case 'j':
            if (action != ACT_NONE) {
                err_multi_action(ACT_READ_PROTECT);
                return 1;
            }
            action = ACT_READ_PROTECT;
            break;

        case 'k':
            if (action != ACT_NONE) {
                err_multi_action(ACT_READ_UNPROTECT);
                return 1;
            }
            action = ACT_READ_UNPROTECT;
            break;

        case 'o':
            if (action != ACT_NONE) {
                err_multi_action(ACT_ERASE_ONLY);
                return 1;
            }
            action = ACT_ERASE_ONLY;
            break;

        case 'v':
            verify = 1;
            break;

        case 'n':
            retry = strtoul(optarg, NULL, 0);
            break;

        case 'g':
            exec_flag = 1;
            execute   = strtoul(optarg, NULL, 0);
            if (execute % 4 != 0) {
                fprintf(stderr, "ERROR: Execution address must be word-aligned\n");
                return 1;
            }
            break;
        case 's':
            if (readwrite_len || start_addr) {
                fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
                return 1;
            }
            spage    = strtoul(optarg, NULL, 0);
            break;
        case 'S':
            if (spage || npages) {
                fprintf(stderr, "ERROR: Invalid options, can't specify start page / num pages and start address/length\n");
                return 1;
            } else {
                start_addr = strtoul(optarg, &pLen, 0);
                if (start_addr % 4 != 0) {
                    fprintf(stderr, "ERROR: Start address must be word-aligned\n");
                    return 1;
                }
                /* we decode 0 as 1 (which is unaligned and thus invalid anyway)
                     * to flag that it was set by the user */
                if (pLen != optarg && start_addr == 0)
                    start_addr = 1;
                if (*pLen == ':') {
                    pLen++;
                    readwrite_len = strtoul(pLen, NULL, 0);
                    if (readwrite_len == 0) {
                        fprintf(stderr, "ERROR: Invalid options, can't specify zero length\n");
                        return 1;
                    }
                }
            }
            break;
        case 'F':
            port_opts.rx_frame_max = strtoul(optarg, &pLen, 0);
            if (*pLen == ':') {
                pLen++;
                port_opts.tx_frame_max = strtoul(pLen, NULL, 0);
            }
            if (port_opts.rx_frame_max < 0
                    || port_opts.tx_frame_max < 0) {
                fprintf(stderr, "ERROR: Invalid negative value for option -F\n");
                return 1;
            }
            if (port_opts.rx_frame_max == 0)
                port_opts.rx_frame_max = STM32_MAX_RX_FRAME;
            if (port_opts.tx_frame_max == 0)
                port_opts.tx_frame_max = STM32_MAX_TX_FRAME;
            if (port_opts.rx_frame_max < 20
                    || port_opts.tx_frame_max < 6) {
                fprintf(stderr, "ERROR: current code cannot work with small frames.\n");
                fprintf(stderr, "min(RX) = 20, min(TX) = 6\n");
                return 1;
            }
            if (port_opts.rx_frame_max > STM32_MAX_RX_FRAME) {
                fprintf(stderr, "WARNING: Ignore RX length in option -F\n");
                port_opts.rx_frame_max = STM32_MAX_RX_FRAME;
            }
            if (port_opts.tx_frame_max > STM32_MAX_TX_FRAME) {
                fprintf(stderr, "WARNING: Ignore TX length in option -F\n");
                port_opts.tx_frame_max = STM32_MAX_TX_FRAME;
            }
            break;
        case 'f':
            force_binary = 1;
            break;

        case 'c':
            init_flag = 0;
            break;

        case 'h':
            show_help();
            exit(0);

        case 'i':
            gpio_seq = optarg;
            break;

        case 'R':
            reset_flag = 1;
            break;

        case 'C':
            if (action != ACT_NONE) {
                err_multi_action(ACT_CRC);
                return 1;
            }
            action = ACT_CRC;
            break;
        }
    }

    return 0;

}
DLL_EXPORT(int) set_device(char * device_name)
{
    static char device_all[200];
    memcpy(device_all, device_name, strlen(device_name));
    device_all[strlen(device_name)] = 0;
    port_opts.device = device_all;
    return 0;
}

int parse_options(char *prog_name)
{
    int c;
    char *pLen;



    if (port_opts.device == NULL) {
        fprintf(stderr, "ERROR: Device not specified\n");
        return 1;
    }

    if ((action != ACT_WRITE) && verify) {
        fprintf(stderr, "ERROR: Invalid usage, -v is only valid when writing\n");
        return 1;
    }

    return 0;
}

DLL_EXPORT(void) show_help() {
    char *name = "stm32flash";
    fprintf(stderr,
            "Usage: %s [-bvngfhc] [-[rw] filename] [tty_device | i2c_device]\n"
            "	-a bus_address	Bus address (e.g. for I2C port)\n"
            "	-b rate		Baud rate (default 57600)\n"
            "	-m mode		Serial port mode (default 8e1)\n"
            "	-r filename	Read flash to file (or - stdout)\n"
            "	-w filename	Write flash from file (or - stdout)\n"
            "	-C		Compute CRC of flash content\n"
            "	-u		Disable the flash write-protection\n"
            "	-j		Enable the flash read-protection\n"
            "	-k		Disable the flash read-protection\n"
            "	-o		Erase only\n"
            "	-e n		Only erase n pages before writing the flash\n"
            "	-v		Verify writes\n"
            "	-n count	Retry failed writes up to count times (default 10)\n"
            "	-g address	Start execution at specified address (0 = flash start)\n"
            "	-S address[:length]	Specify start address and optionally length for\n"
            "	                   	read/write/erase operations\n"
            "	-F RX_length[:TX_length]  Specify the max length of RX and TX frame\n"
            "	-s start_page	Flash at specified page (0 = flash start)\n"
            "	-f		Force binary parser\n"
            "	-h		Show this help\n"
            "	-c		Resume the connection (don't send initial INIT)\n"
            "			*Baud rate must be kept the same as the first init*\n"
            "			This is useful if the reset fails\n"
            "	-R		Reset device at exit.\n"
            "	-i GPIO_string	GPIO sequence to enter/exit bootloader mode\n"
            "			GPIO_string=[entry_seq][:[exit_seq]]\n"
            "			sequence=[[-]signal]&|,[sequence]\n"
            "\n"
            "GPIO sequence:\n"
            "	The following signals can appear in a sequence:\n"
            "	  Integer number representing GPIO pin\n"
            "	  'dtr', 'rts' or 'brk' representing serial port signal\n"
            "	The sequence can use the following delimiters:\n"
            "	  ',' adds 100 ms delay between signals\n"
            "	  '&' adds no delay between signals\n"
            "	The following modifiers can be prepended to a signal:\n"
            "	  '-' reset signal (low) instead of setting it (high)\n"
            "\n"
            "Examples:\n"
            "	Get device information:\n"
            "		%s /dev/ttyS0\n"
            "	  or:\n"
            "		%s /dev/i2c-0\n"
            "\n"
            "	Write with verify and then start execution:\n"
            "		%s -w filename -v -g 0x0 /dev/ttyS0\n"
            "\n"
            "	Read flash to file:\n"
            "		%s -r filename /dev/ttyS0\n"
            "\n"
            "	Read 100 bytes of flash from 0x1000 to stdout:\n"
            "		%s -r - -S 0x1000:100 /dev/ttyS0\n"
            "\n"
            "	Start execution:\n"
            "		%s -g 0x0 /dev/ttyS0\n"
            "\n"
            "	GPIO sequence:\n"
            "	- entry sequence: GPIO_3=low, GPIO_2=low, 100ms delay, GPIO_2=high\n"
            "	- exit sequence: GPIO_3=high, GPIO_2=low, 300ms delay, GPIO_2=high\n"
            "		%s -i '-3&-2,2:3&-2,,,2' /dev/ttyS0\n"
            "	GPIO sequence adding delay after port opening:\n"
            "	- entry sequence: delay 500ms\n"
            "	- exit sequence: rts=high, dtr=low, 300ms delay, GPIO_2=high\n"
            "		%s -R -i ',,,,,:rts&-dtr,,,2' /dev/ttyS0\n",
            name,
            name,
            name,
            name,
            name,
            name,
            name,
            name,
            name
            );
}

