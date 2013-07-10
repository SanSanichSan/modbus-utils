#include <stdio.h>     /* for printf */
#include <stdlib.h>    /* for exit */
#include <getopt.h>
#include <string.h>
#include <stdint.h>

#include "modbus.h"

const char DebugOpt[]   = "debug";
const char TcpOptVal[]  = "tcp";
const char RtuOptVal[]  = "rtu";

enum ConnType {
    None,
    Tcp,
    Rtu
};

enum FuncType {
    FuncNone =          -1,

    ReadCoils           = 0x01,
    ReadDiscreteInput   = 0x02,
    ReadHoldingRegisters= 0x03,
    ReadInputRegisters  = 0x04,
    WriteSingleCoil     = 0x05,
    WriteSingleRegister = 0x06,
    WriteMultipleCoils  = 0x0f,
    WriteMultipleRegisters  = 0x10
};

void printUsage(const char progName[]) {
    printf("%s [--%s] [-m {rtu|tcp}] [-a<slave-addr=1>] [-c<read-no>=1]\n\t" \
           "[-r<start-addr>=100] [-t<data-type>] [-o<timeout-ms>=1000] [{rtu-params|tcp-params}] serialport|host [<write-data>]\n", progName, DebugOpt);
    printf("f-type:\n" \
           "\t(0x01) Read Coils, (0x02) Read Discrete Inputs, (0x05) Write Single Coil\n" \
           "\t(0x03) Read Holding Registers, (0x04) Read Input Registers, (0x06) WriteSingle Register\n" \
           "\t(0x0F) WriteMultipleCoils, (0x10) Write Multiple register\n");
    printf("rtu-params:\n" \
           "\tb<baud-rate>=9600\n" \
           "\td{7|8}<data-bits>=8\n" \
           "\ts{1|2}<stop-bits>=1\n" \
           "\tp{none|even|odd}=even\n");
    printf("tcp-params:\n" \
           "\tp<port>=502\n");
}

int getInt(const char str[], int *ok) {
    int value;
    int ret = sscanf(str, "0x%x", &value);
    if (0 == ret) {//couldn't convert from hex, try dec
        ret = sscanf(str, "%d", &value);
    }

    if (0 != ok) {
        *ok = (0 != ret);
    }

    return value;
}

struct BackendParams {
    ConnType type;
};

struct RtuBackend {
    ConnType type;
    char devName[32];
    int baud;
    int dataBits;
    int stopBits;
    char parity;
};

BackendParams *initRtuParams(RtuBackend *rtuParams) {
    rtuParams->type = Rtu;
    strcpy(rtuParams->devName, "");
    rtuParams->baud = 9600;
    rtuParams->dataBits = 8;
    rtuParams->stopBits = 1;
    rtuParams->parity = 'e';

    return (BackendParams*)rtuParams;
}

int setRtuParam(RtuBackend* rtuParams, char c, char *value) {
    int ok = 1;

    switch (c) {
    case 'b': {
        rtuParams->baud = getInt(value, &ok);
        if (0 != ok) {
            printf("Baudrate is invalid %s", value);
            ok = 0;
        }
    }
        break;
    case 'd': {
        int db = getInt(value, &ok);
        if (0 == ok || (7 != db && 8 != db)) {
            printf("Data bits incorrect (%s)", value);
            ok = 0;
        }
        else
            rtuParams->dataBits = db;
    }
        break;
    case 's': {
        int sb = getInt(value, &ok);
        if (0 == ok || (1 != sb && 2 != sb)) {
            printf("Stop bits incorrect (%s)", value);
            ok = 0;
        }
        else
            rtuParams->stopBits = sb;
    }
        break;
    default:
        printf("Unknown rtu param (%c: %s)\n\n", c, value);
        ok = 0;
    }

    return ok;
}

struct TcpBackend {
    ConnType type;
    char ip[32];
    int port;
};

BackendParams *initTcpParams(TcpBackend *tcpParams) {
    tcpParams->type = Tcp;
    strcpy(tcpParams->ip, "0.0.0.0");
    tcpParams->port = 502;

    return (BackendParams*)tcpParams;
}

int main(int argc, char **argv)
{
    int c;
    int ok;

    int debug = 0;
    BackendParams *backend = 0;
    int slaveAddr = 1;
    int startAddr = 100;
    int readWriteNo = -1;
    int fType = FuncNone;
    int timeout_ms = 1000;
    int hasDevice = 0;

    int isWriteFunction = 0;
    enum WriteDataType {
        DataInt,
        Data8Array,
        Data16Array
    } wDataType = DataInt;
    union Data {
        int dataInt;
        uint8_t *data8;
        uint16_t *data16;
    } data;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {DebugOpt,  no_argument, 0,  0},
            {0, 0,  0,  0}
        };

        c = getopt_long(argc, argv, "a:b:d:c:m:r:s:t:p:o",
                        long_options, &option_index);
        if (c == -1) {
            printUsage(argv[0]);
            break;
        }

        switch (c) {
        case 0:
            if (0 == strcmp(long_options[option_index].name, DebugOpt)) {
                debug = 1;
            }
            break;

        case 'a': {
            slaveAddr = getInt(optarg, &ok);
            if (0 == ok) {
                printf("Slave address (%s) is not integer!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
            break;

        case 'c': {
            readWriteNo = getInt(optarg, &ok);
            if (0 == ok) {
                printf("# elements to read/write (%s) is not integer!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }

        case 'm':
            if (0 == strcmp(optarg, TcpOptVal))
                backend = initTcpParams(new TcpBackend);
            else if (0 == strcmp(optarg, RtuOptVal))
                backend = initRtuParams(new RtuBackend);
            else {
                printf("Unrecognized connection type %s\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;

        case 'r': {
            startAddr = getInt(optarg, &ok);
            if (0 == ok) {
                printf("Start address (%s) is not integer!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
            break;

        case 't': {
            fType = getInt(optarg, &ok);
            if (0 == ok) {
                printf("Function type (%s) is not integer!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
            break;

        case 'o': {
            timeout_ms = getInt(optarg, &ok);
            if (0 == ok) {
                printf("Timeout (%s) is not integer!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
            break;
            //tcp/rtu params
        case 'p': {
            if (Tcp == backend->type) {
                TcpBackend *tcpP = (TcpBackend*)backend;
                tcpP->port = getInt(optarg, &ok);
                if (0 == ok) {
                    printf("Port parameter %s is not integer!\n\n", optarg);
                    printUsage(argv[0]);
                    exit(EXIT_FAILURE);
                }
            }
            else if (Rtu == backend->type) {
                if (0 == setRtuParam((RtuBackend*)backend, c, optarg))
                    exit(EXIT_FAILURE);
            }
            else {
                printf("Port parameter %s specified for non TCP or RTU conn type!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
            //rtu params
        case 'b':
        case 'd':
        case 's':
            if (Rtu == backend->type) {
                if (0 == setRtuParam((RtuBackend*)backend, c, optarg)) {
                    exit(EXIT_FAILURE);
                }
            }
            else {
                printf("Port parameter %s specified for non RTU conn type!\n\n", optarg);
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;
        case '?':
            break;

        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    //choose write data type
    switch (fType) {
    case(ReadCoils):
        wDataType = Data8Array;
        break;
    case(ReadDiscreteInput):
        wDataType = DataInt;
        break;
    case(ReadHoldingRegisters):
    case(ReadInputRegisters):
        wDataType = Data16Array;
        break;
    case(WriteSingleCoil):
    case(WriteSingleRegister):
        wDataType = DataInt;
        isWriteFunction = 1;
        break;
    case(WriteMultipleCoils):
        wDataType = Data8Array;
        isWriteFunction = 1;
        break;
    case(WriteMultipleRegisters):
        wDataType = Data16Array;
        isWriteFunction = 1;
        break;
    default:
        printf("No correct function type chosen");
        printUsage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (isWriteFunction) {
        int dataNo = argc - optind - 1;
        if (-1 != readWriteNo && dataNo != readWriteNo) {
            printf("Write count specified, not equal to data values count!");
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
        }
        else readWriteNo = dataNo;
    }

    //allocate buffer for data
    switch (wDataType) {
    case (DataInt):
        //no need to alloc anything
        break;
    case (Data8Array):
        data.data8 = new uint8_t[readWriteNo];
        break;
    case (Data16Array):
        data.data16 = new uint16_t[readWriteNo];
        break;
    default:
        printf("Data alloc error!\n");
        exit(EXIT_FAILURE);
    }

    int wDataIdx = 0;
    if (optind < argc) {
        while (optind < argc) {
            if (0 == hasDevice) {
                if (0 != backend) {
                    if (Rtu == backend->type) {
                        RtuBackend *rtuP = (RtuBackend*)backend;
                        strcpy(rtuP->devName, argv[optind]);
                        hasDevice = 0;
                    }
                    else if (Tcp == backend->type) {
                        TcpBackend *tcpP = (TcpBackend*)backend;
                        strcpy(tcpP->ip, argv[optind]);
                        hasDevice = 0;
                    }
                }
            }
            else {//getting data
                switch (wDataType) {
                case (DataInt):
                    data.dataInt = getInt(argv[optind], 0);
                    break;
                case (Data8Array):
                    data.data8[wDataIdx] = getInt(argv[optind], 0);
                    break;
                case (Data16Array):
                    data.data16[wDataIdx] = getInt(argv[optind], 0);
                    break;
                }
                wDataIdx++;
            }
            optind++;
        }
    }

    //create modbus context

    modbus_t *ctx = 0;
    if (Rtu == backend->type) {
        RtuBackend *rtu = (RtuBackend*)backend;
        ctx = modbus_new_rtu(rtu->devName, rtu->baud, rtu->parity, rtu->dataBits, rtu->stopBits);
    }
    else if (Tcp == backend->type) {
        TcpBackend *tcp = (TcpBackend*)backend;
        ctx = modbus_new_tcp(tcp->ip, tcp->port);
    }

    modbus_set_debug(ctx, debug);
    modbus_set_slave(ctx, slaveAddr);

    int ret = -1;
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    } else {
        switch (fType) {
        case(ReadCoils):
            ret = modbus_read_bits(ctx, startAddr, readWriteNo, data.dataInt);
            break;
        case(ReadDiscreteInput):
            wDataType = DataInt;
            break;
        case(ReadHoldingRegisters):
        case(ReadInputRegisters):
            wDataType = Data16Array;
            break;
        case(WriteSingleCoil):
        case(WriteSingleRegister):
            wDataType = DataInt;
            isWriteFunction = 1;
            break;
        case(WriteMultipleCoils):
            wDataType = Data8Array;
            isWriteFunction = 1;
            break;
        case(WriteMultipleRegisters):
            wDataType = Data16Array;
            isWriteFunction = 1;
            break;
        default:
            printf("No correct function type chosen");
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    //cleanup
    if (0 != backend) {
        if (Rtu == backend->type)
            delete (RtuBackend*)backend;
        else if (Tcp == backend->type)
            delete (TcpBackend*)backend;
    }

    switch (wDataType) {
    case (DataInt):
        //nothing to be done
        break;
    case (Data8Array):
        delete data.data8;
        break;
    case (Data16Array):
        delete [] data.data16;
        break;
    }

    exit(EXIT_SUCCESS);
}
