#include <Windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <random>
#include <string>

struct Bmi {

    BITMAPINFOHEADER bmiHeader;
    RGBQUAD colortable[256]; // custom colortable for bitmap so 255 is white and 0 is black
};

Bmi bmi;

const unsigned char keypad[16] =
{
    0x31, // 1
    0x32, // 2
    0x33, // 3
    0x34, // 4
    0x51, // q
    0x57, // w
    0x45, // e
    0x52, // r
    0x41, // a
    0x53, // s
    0x44, // d
    0x46, // f
    0x59, // y
    0x58, // x
    0x43, // c
    0x56, // v
};

void playSound()
{
    Beep(750, 150);
}

const int width = 64;
const int height = 32;

const char* windowname = "CHIP-8 Emulator";
const int upscalefactor = 10; // 10 would be 640 * 320 

unsigned char upscaledgraphic10x[(width * upscalefactor) * (height * upscalefactor)];

std::random_device random_device; // establishes the random device
std::mt19937 generator(random_device()); // initializes the algorithm with the random device
std::uniform_int_distribution<> distribution1(1, 100); // creates random number between 1 and 100

const float target_fps = 60.0f;
const std::chrono::duration<float> frame_duration(1 / target_fps);
const int cyclesPerFrame = 10;

void restartApp() {
    char path[MAX_PATH]; //  declares puffer to save path to current .exe
    GetModuleFileNameA(NULL, path, MAX_PATH); // grabs the full path to current .exe

    int argc = __argc;
    char** argv = __argv;

    std::string manipulatedArgs; // necessary to filter out the program name twice
    for (int i = 1; i < argc; ++i)
    {
        manipulatedArgs += "\"";
        manipulatedArgs += argv[i];
        manipulatedArgs += "\"";
    }

    ShellExecuteA(NULL, "open", path, manipulatedArgs.c_str(), NULL, SW_SHOWNORMAL); // starts program again with the same arguments
    ExitProcess(0);
}


void initBMI()
{
    for (int i = 0; i < 256; ++i) {
        bmi.colortable[i].rgbBlue = i;
        bmi.colortable[i].rgbGreen = i;
        bmi.colortable[i].rgbRed = i;
        bmi.colortable[i].rgbReserved = 0;
    }

    // Initialize BITMAPINFOHEADER
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (width * upscalefactor);
    bmi.bmiHeader.biHeight = ~(height * upscalefactor); // Negative value to draw top to bottom
    bmi.bmiHeader.biPlanes = 1; // always 1
    bmi.bmiHeader.biBitCount = 8; // 8 bits per pixel (256 colors)
    bmi.bmiHeader.biCompression = BI_RGB;
}

/*
0x000-0x1FF - Chip 8 interpreter (contains font set in emu)
0x050-0x0A0 - Used for the built in 4x5 pixel font set (0-F)
0x200-0xFFF - Program ROM and work RAM
*/

unsigned char chip8_fontset[80] =
{
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

class chip8
{
public:

    // Sets up the System with memory, registers etc...
    unsigned char memory[4096]; // 0 - 511 are for the interpreter originally / emulators store font at 0 and game at 512 and upwards usually 
    unsigned char graphic[width * height];

    unsigned char data_registers[16]{}; // V0 - VF registers last one VF used for carry flag 8 bit
    unsigned short index = 0; // address register normally 12 bits wide, here 16 bits wide (short)
    unsigned short stack[16] = { 0 }; // stack used to store return addresses when subroutines are called typical 12 layers or 16
    unsigned char sp = 0;// stackpointer holding the actually stack position

    //both can have values from 0x000 to 0xFFF or 0 - 4095
    unsigned short opcode = 0; //fetch opcode which is 12 bits here 16 bits 2 bytes (short)
    unsigned short pc = 0x200; // starts at 512 - program counter which is 12 bits here 16 bits 2 bytes (short)

    // unsigned char graphic[64 * 32]; // black and white graphics with a total of 2048 pixels (64*32)
    unsigned char key[16]; //hex based keypad holding values 0 - 15

    bool carryFlagSet = false;

    unsigned char delay_timer = 0; // counts down from 60 to 0
    unsigned char sound_timer = 0; // counts down from 60 to 0 and gives beep noise when 0

    //loads fontset into memory - has to be placed her to make sure setup was called
    void loadFontset() {
        for (int i = 0; i < 80; ++i)
        {
            memory[i] = chip8_fontset[i];
        }
    }

    void clearDisplay()
    {
        for (int i = 0; i < sizeof(graphic); ++i)
        {
            graphic[i] = 0;
        }
    }

    void executecycle()
    {
        opcode = memory[pc] << 8 | memory[pc + 1]; // 2 * 8 bits 16 bit code. only 12 bits needed
        unsigned char opcodeDecode = (opcode >> 12); // & 0X00F shift 12 times right to leave 4
        pc += 2; // increment program counter here, so it doesnt have to be in every case unless the pc is increment by more than 2

        switch (opcodeDecode)
        {
        case 0:

            switch (opcode & 0xFF)
            {
            case 0xE0: // 224
                clearDisplay(); // sets entire display to 0 values
                carryFlagSet = true;
                break;

            case 0xEE: // 238
                --sp;
                pc = stack[sp];
                break;
            }
            break;

        case 1:
            pc = opcode & 0xFFF; // 1NNN uses highest 12 bit number 4095 to set off first 4 bits
            break;

        case 2:
            stack[sp] = pc;
            ++sp;
            pc = opcode & 0xFFF;
            break;

        case 3: // 3XNN
            if (data_registers[(opcode >> 8) & 0X00F] == (opcode & 0xFF))
            {
                pc += 2;
            }
            break;

        case 4: // 4XNN
            if (data_registers[(opcode >> 8) & 0X00F] != (opcode & 0xFF))
            {
                pc += 2;
            }
            break;

        case 5: // 5XYO
            if (data_registers[(opcode >> 8) & 0X00F] == data_registers[(opcode >> 4) & 0X00F])
            {
                pc += 2;
            }
            break;

        case 6:
            data_registers[(opcode >> 8) & 0X00F] = (opcode & 0xFF); // 6XNN same as case 7 basically
            break;

        case 7:
            data_registers[(opcode >> 8) & 0X00F] += (opcode & 0xFF); // 7XNN shift second nibble right, then offset 1 nibble with 15, extract
            // last 8 bits for address
            break;

        case 8:
            switch (opcode & 0X00F)
            {
            case 0:      // 8XYO
                data_registers[(opcode >> 8) & 0X00F] = data_registers[(opcode >> 4) & 0X00F];
                break;

            case 1:       // 8XY1
                data_registers[(opcode >> 8) & 0X00F] |= data_registers[(opcode >> 4) & 0X00F];
                break;

            case 2:
                data_registers[(opcode >> 8) & 0X00F] &= data_registers[(opcode >> 4) & 0X00F];
                break;

            case 3:
                data_registers[(opcode >> 8) & 0X00F] ^= data_registers[(opcode >> 4) & 0X00F];
                break;

            case 4:
            {
                unsigned char temp_reg = data_registers[(opcode >> 8) & 0X00F];
                data_registers[(opcode >> 8) & 0X00F] += data_registers[(opcode >> 4) & 0X00F];
                if (data_registers[(opcode >> 8) & 0X00F] < temp_reg)
                {
                    data_registers[0X00F] = 1;
                }
                else
                    data_registers[0X00F] = 0;
            }
            break;

            case 5:
            {
                unsigned char temp_reg2 = data_registers[(opcode >> 8) & 0X00F];
                data_registers[(opcode >> 8) & 0X00F] -= data_registers[(opcode >> 4) & 0X00F];
                if (data_registers[(opcode >> 8) & 0X00F] > temp_reg2)
                {
                    data_registers[0X00F] = 0;
                }
                else
                    data_registers[0X00F] = 1;
            }
            break;

            case 6:
                data_registers[0X00F] = data_registers[(opcode >> 8) & 0X00F] & 0x1;
                data_registers[(opcode >> 8) & 0X00F] >>= 1;
                break;

            case 7:
                data_registers[(opcode >> 8) & 0X00F] = (data_registers[(opcode >> 4) & 0X00F] - data_registers[(opcode >> 8) & 0X00F]);
                if ((data_registers[(opcode >> 4) & 0X00F] - data_registers[(opcode >> 8) & 0X00F]) < 0)
                {
                    data_registers[0X00F] = 0;
                }
                else
                    data_registers[0X00F] = 1;
                break;

            case 14:
                if ((data_registers[(opcode >> 8) & 0X00F] >> 7) == 1)
                {
                    data_registers[0X00F] = 1;
                }
                else {
                    data_registers[0X00F] = 0;
                }
                data_registers[(opcode >> 8) & 0X00F] <<= 1;
                break;
            }
            break;

        case 9:
            if (data_registers[(opcode >> 8) & 0X00F] != data_registers[(opcode >> 4) & 0X00F])
            {
                pc += 2;
            }
            break;

        case 10:
            index = opcode & 0xFFF; // ANNN uses highest 12 bit number 4095 to set off first 4 bits
            break;

        case 11:
            pc = (opcode & 0xFFF) + data_registers[0x0];
            break;

        case 12:
        {
            int random_number = distribution1(generator);
            data_registers[(opcode >> 8) & 0X00F] = random_number & (opcode & 0xFF); // not rand() & 255 & (opcode & 0xFF) !!!!
        }
        break;

        case 13: // DXYN
        {
            unsigned char nibble2 = (opcode >> 8) & 0X00F; // X = shift right 8 times and extract first 4 bits with & 15
            unsigned char nibble3 = (opcode >> 4) & 0X00F; // Y = shift right 4 times and extract first 4 bits with & 15
            unsigned char height = opcode & 0X00F; // Nibble 4      N = extract last 4 bits with & 15
            unsigned char width = 8; // sprite draw with width of 8 and height of N at coordinate  VX and VY (see above) // always 8 pixels
            unsigned char VX = data_registers[nibble2] % 64; //loads value of register VX. Could be bigger than 64 screen width thats why modulo
            unsigned char VY = data_registers[nibble3] % 32; //loads value of register VY. Could be bigger than 32 screen height thats why modulo

            data_registers[0X00F] = 0; // data register VF 15 reset
            unsigned char pixel;
            unsigned char countdownpixelbits = 7; // 7 shift shift are made to extract the single bits from pixel address values looped
            for (int i = 0; i < height; i++) // width * height (nibble 4) height  count of memory addresses
            {
                pixel = memory[index + i]; // loads sprite from memory address

                // loops through a sprite which is always 8 bit thats why width const 8
                for (int y = 0; y < width; ++y)
                {
                    if ((VX + y) < 64 && (VY + i) < 32) // checks if width and height are within 64*32 -- VX + y width -- VY + i height
                    {
                        if (((pixel >> countdownpixelbits) & 1))// shift every pixel from left to right and filters them out with &
                        {                                             //  pixel & 128 possible, because max number 255 char 
                                                                      //    and will either return 0 or 1 and greater      
                            // height * max width + width
                            int pixelpos = ((VY + i) * 64 + (VX + y)); // modulo to make sure its not above the highest puffer address of 2048 (64*32)


                            if (graphic[pixelpos])
                            {
                                data_registers[0X00F] = 1;
                            }
                            graphic[pixelpos] ^= 255; // max value 255 for white 0 would be black

                        }
                    }
                    countdownpixelbits = countdownpixelbits - 1; // decreases one to get next bit
                }
                countdownpixelbits = 7; // resets  count to 7 for the next row of 8 bits

            }
            carryFlagSet = true;
        }
        break;

        case 14: // EX9E and EXA1
            switch (opcode & 0xFF)
            {
            case 0x9E: // 158 skips next instruction if key is pressed
                if (key[data_registers[(opcode >> 8) & 0X00F]] != 0)
                {
                    pc += 2;
                }
                break;

            case 0xA1: // 161 skips next instruction if key is not pressed
                if (key[data_registers[(opcode >> 8) & 0X00F]] == 0)
                {
                    pc += 2;
                }
                break;
            }
            break;
        case 15:
            switch (opcode & 0xFF)
            {
            case 0x07: // 7
                data_registers[(opcode >> 8) & 0X00F] = delay_timer;
                break;

            case 0x0A: // 10
            {
                bool key_was_pressed = false;

                for (int i = 0; i < 16; ++i)
                {
                    if (key[i] != 0)
                    {
                        data_registers[(opcode >> 8) & 0X00F] = i;
                        key_was_pressed = true;
                    }
                }

                if (key_was_pressed == false)
                {
                    pc -= 2;
                }
            }
            break;

            case 0x15: // 21
                delay_timer = data_registers[(opcode >> 8) & 0X00F];
                break;

            case 0x18: // 24
                sound_timer = data_registers[(opcode >> 8) & 0X00F];
                break;

            case 0x1E: // 30
                index += data_registers[(opcode >> 8) & 0X00F];
                break;

            case 0x29: // 41
                index = data_registers[(opcode >> 8) & 0X00F] * 0x5; // Every character is 5 bytes each thats why * 0x5
                break;
            case 0x33: // 51      example number 125 ub register X 125

                memory[index] = data_registers[(opcode >> 8) & 0X00F] / 100; // gets the first number 1 from 125
                memory[index + 1] = (data_registers[(opcode >> 8) & 0X00F] / 10) % 10; // gets the second number 2 from 125
                memory[index + 2] = data_registers[(opcode >> 8) & 0X00F] % 10; // gets the third number 5 from 125
                break;

            case 0x55: // 85
                for (int i = 0; i <= ((opcode >> 8) & 0X00F); ++i)
                {
                    (memory[index + i]) = data_registers[i];
                }
                break;

            case 0x65: // 101
                for (int i = 0; i <= ((opcode >> 8) & 0X00F); ++i)
                {
                    data_registers[i] = (memory[index + i]);
                }
                break;
            }
            break;
        }
    }
};

chip8 Chip8;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        for (int i = 0; i < 16; i++)
        {
            if (wParam == keypad[i])
            {
                Chip8.key[i] = 1;
            }
            else if (wParam == 0x1B) // ESC
            {
                exit(0);
            }
            else if (wParam == 0x74) // F5
            {
                restartApp();
            }
        }
        return 0;

    case WM_KEYUP:
        for (int i = 0; i < 16; i++)
        {
            if (wParam == keypad[i])
            {
                Chip8.key[i] = 0;
            }
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; // structure for painting

        // Prepares window for painting and fill ps structure with info about the painting
        HDC hdc = BeginPaint(hwnd, &ps);

        EndPaint(hwnd, &ps); // marks end of painting
        return 0;
    }
    break;

    case WM_SIZE:
    {
        // Making sure it start radrawing after being minimized and brought back
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        return 0;
    }
    break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// WINAPI is a marco for the calling convention (--stdcall)
int WINAPI WinMain(HINSTANCE hInstance, // something like the ID for the OS to identify program
    HINSTANCE hPrevInstance, // used in 16 bit Windows, nowadays always NULL
    LPSTR lpCmdLine, // contains passed arguments  
    int nCmdShow) // controls how the window is shown - maximized, minimized, normal
{
    initBMI();
    Chip8.loadFontset();

    // build in macros
    int argc = __argc;
    char** argv= __argv;

    if (argc != 2)
    {
        return 1;
    }
    else
    {
        const char* gameFile = argv[1];

        std::ifstream game(gameFile, std::ios::binary | std::ios::ate);
        if (game)
        {
            // start at address 512 to load in bytes of the game. Break if size > 4096
            std::streampos gamesize = game.tellg();
            if ((gamesize += 512) > 4096)
            {
                return 1;
            }
            else
            {
                game.seekg(0, std::ios_base::beg);
                game.read(reinterpret_cast<char*>(&Chip8.memory[0x200]), gamesize);
            }
        }
    }
    // Define the window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc; // assings the window behavior to the window
    wc.hInstance = hInstance; // assigns the "ID" to window
    wc.lpszClassName = windowname; // window class name

    if (!RegisterClass(&wc)) // checks if windows class you be created
    {
        return 1;
    }

    int visiblewidth = (width * upscalefactor);
    int visibleheight = (height * upscalefactor);

    RECT rect = { 0,0, visiblewidth, visibleheight }; // create rectangle (left, top, right, bottom)
    // make rect usuable as windows outer size
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE); // rect object, ws style, has menu or not

    int windowwidth = rect.right - rect.left;
    int windowheight = rect.bottom - rect.top;

    // Create the window
    HWND hwnd = CreateWindow(
        wc.lpszClassName, // Window class name
        wc.lpszClassName, // Windows name
        WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX, // windows style 
        (GetSystemMetrics(SM_CXSCREEN) - (width * upscalefactor)) / 2, (GetSystemMetrics(SM_CYSCREEN) - (height * upscalefactor)) / 2, // x and y where on screen - CW_USEDEFAULT, CW_USEDEFAULT (default), here centered
        windowwidth, windowheight, // width and height
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd)
    {
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, nCmdShow); // created window and how window is shown (argument main)
    UpdateWindow(hwnd); // create window as argument

    MSG msg{}; // create class which holds input to window
    while (true)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) // retrieves messages from the queue until its 0
        {
            if (msg.message == WM_QUIT)
            {
                return 0;
            }
            TranslateMessage(&msg); // translates the virtual key mess into character messages
            DispatchMessage(&msg); // dispatches the message to the window procedure function
        }
        auto frame_start = std::chrono::high_resolution_clock::now(); // start the clock to measure frametime

        for (int i = 0; i < cyclesPerFrame; i++)
        {
            Chip8.executecycle();
        }

        if (Chip8.carryFlagSet)
        {
            Chip8.carryFlagSet = false;

            unsigned char* ptr = upscaledgraphic10x; 
           
            for (int y = 0; y < height; ++y) {
                for (int i = 0; i < upscalefactor; ++i) {
                    for (int x = 0; x < width; x++) {
                        memset(ptr, Chip8.graphic[y * width + x], upscalefactor); // to copy more than one byte at once
                        ptr += (upscalefactor);
                    }
                }
            }

            HDC hdc = GetDC(hwnd);

            SetDIBitsToDevice(hdc, 0, 0, (width * upscalefactor), (height * upscalefactor), 0, 0, 0, (height * upscalefactor), upscaledgraphic10x, (BITMAPINFO*)&bmi, DIB_RGB_COLORS);

            ReleaseDC(hwnd, hdc);
        }

        if (Chip8.delay_timer > 0)
            --Chip8.delay_timer;

        if (Chip8.sound_timer > 0)
        {
            --Chip8.sound_timer;

            if (Chip8.sound_timer != 0)
            {
                Chip8.sound_timer = 0;
                std::thread beepThread(playSound);
                beepThread.detach();
            }
        }

        auto frame_end = std::chrono::high_resolution_clock::now(); // ends clock to measure time past
        std::chrono::duration<double> elapsed = frame_end - frame_start; // calculates difference

        // busy waiting until 16.7ms have passed to maintain 60fps
        while (elapsed < frame_duration) {

            frame_end = std::chrono::high_resolution_clock::now();
            elapsed = frame_end - frame_start;
        } 
    }
    return 0;
}