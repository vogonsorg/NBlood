/*
 * control.c
 * MACT library controller handling
 *
 * Derived from MACT386.LIB disassembly by Jonathon Fowler
 *
 */

#include "control.h"

#include "_control.h"
#include "baselayer.h"
#include "build.h"
#include "common.h"
#include "compat.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "osd.h"
#include "pragmas.h"

#ifdef __ANDROID__
#include "android.h"
#endif

// TODO: add mact cvars and make this user configurable
#define USERINPUTDELAY 500
#define USERINPUTFASTDELAY 60

bool CONTROL_Started         = false;
bool CONTROL_MouseEnabled    = false;
bool CONTROL_MousePresent    = false;
bool CONTROL_JoyPresent      = false;
bool CONTROL_JoystickEnabled = false;
bool CONTROL_JoystickConsoleSpam = false;

uint64_t CONTROL_ButtonState     = 0;
uint64_t CONTROL_ButtonHeldState = 0;

LastSeenInput CONTROL_LastSeenInput;

static int32_t CONTROL_UserInputDelay = -1;
float          CONTROL_MouseSensitivity = DEFAULTMOUSESENSITIVITY;
float          CONTROL_MouseAxesSensitivity[2];
static int32_t CONTROL_NumMouseButtons  = 0;
static int32_t CONTROL_NumJoyButtons    = 0;
static int32_t CONTROL_NumJoyAxes       = 0;

static controlflags      CONTROL_Flags[CONTROL_NUM_FLAGS];

static controlkeymaptype  CONTROL_KeyMapping[CONTROL_NUM_FLAGS];

static controlaxismaptype CONTROL_JoyAxesMap[MAXJOYAXES];
static controlaxistype    CONTROL_JoyAxes[MAXJOYAXES];
static controlaxistype    CONTROL_LastJoyAxes[MAXJOYAXES];
static float              CONTROL_JoyAxesSensitivity[MAXJOYAXES];
static int8_t             CONTROL_JoyAxesInvert[MAXJOYAXES];

uint16_t                  CONTROL_JoyDeadZone[MAXJOYAXES];
uint16_t                  CONTROL_JoySaturation[MAXJOYAXES];

static controlbuttontype CONTROL_MouseButtonMapping[MAXMOUSEBUTTONS];

static int32_t CONTROL_MouseButtonClicked[MAXMOUSEBUTTONS];
static int32_t CONTROL_MouseButtonClickedState[MAXMOUSEBUTTONS];
static int32_t CONTROL_MouseButtonClickedTime[MAXMOUSEBUTTONS];
static int32_t CONTROL_MouseButtonState[MAXMOUSEBUTTONS];
static uint8_t CONTROL_MouseButtonClickedCount[MAXMOUSEBUTTONS];

static controlbuttontype CONTROL_JoyButtonMapping[MAXJOYBUTTONS];

static int32_t CONTROL_JoyButtonClicked[MAXJOYBUTTONS];
static int32_t CONTROL_JoyButtonClickedState[MAXJOYBUTTONS];
static int32_t CONTROL_JoyButtonClickedTime[MAXJOYBUTTONS];
static int32_t CONTROL_JoyButtonState[MAXJOYBUTTONS];
static uint8_t CONTROL_JoyButtonClickedCount[MAXJOYBUTTONS];

static bool      CONTROL_UserInputCleared[4];
static UserInput CONTROL_UserInput;
static direction CONTROL_LastUserInputDirection;
static int32_t (*ExtGetTime)(void);
static int32_t ticrate;
static uint8_t CONTROL_DoubleClickSpeed;

int32_t CONTROL_ButtonFlags[CONTROL_NUM_FLAGS];
consolekeybind_t CONTROL_KeyBinds[MAXBOUNDKEYS + MAXMOUSEBUTTONS];
bool CONTROL_BindsEnabled = 0;

#define CONTROL_CheckRange(which) ((unsigned)which >= (unsigned)CONTROL_NUM_FLAGS)
#define BIND(x, s, r, k) do { Xfree(x.cmdstr); x.cmdstr = s; x.repeat = r; x.key = k; } while (0)

void CONTROL_ClearAllBinds(void)
{
    for (int i=0; i<MAXBOUNDKEYS; i++)
        CONTROL_FreeKeyBind(i);
    for (int i=0; i<MAXMOUSEBUTTONS; i++)
        CONTROL_FreeMouseBind(i);
}

void CONTROL_BindKey(int i, char const * const cmd, int repeat, char const * const keyname)
{
    BIND(CONTROL_KeyBinds[i], Xstrdup(cmd), repeat, keyname);
}

void CONTROL_BindMouse(int i, char const * const cmd, int repeat, char const * const keyname)
{
    BIND(CONTROL_KeyBinds[MAXBOUNDKEYS + i], Xstrdup(cmd), repeat, keyname);
}

void CONTROL_FreeKeyBind(int i)
{
    BIND(CONTROL_KeyBinds[i], NULL, 0, NULL);
}

void CONTROL_FreeMouseBind(int i)
{
    BIND(CONTROL_KeyBinds[MAXBOUNDKEYS + i], NULL, 0, NULL);
}

static void CONTROL_GetMouseDelta(ControlInfo * info)
{
    vec2_t input;
    mouseReadPos(&input.x, &input.y);
    
    vec2f_t finput = { input.x * CONTROL_MouseSensitivity * CONTROL_MouseAxesSensitivity[0] * MOUSESENSITIVITYMULTIPLIER,
                       input.y * CONTROL_MouseSensitivity * CONTROL_MouseAxesSensitivity[1] * MOUSESENSITIVITYMULTIPLIER };

    info->mousex = Blrintf(clamp(finput.x, -MAXSCALEDCONTROLVALUE, MAXSCALEDCONTROLVALUE));
    info->mousey = Blrintf(clamp(finput.y, -MAXSCALEDCONTROLVALUE, MAXSCALEDCONTROLVALUE));
}

static int32_t CONTROL_GetTime(void)
{
    static int32_t t = 0;
    t += 5;
    return t;
}

static void CONTROL_SetFlag(int which, int active)
{
    if (CONTROL_CheckRange(which)) return;

    controlflags &flags = CONTROL_Flags[which];

    if (flags.toggle == INSTANT_ONOFF)
        flags.active = active;
    else if (active)
        flags.buttonheld = FALSE;
    else if (flags.buttonheld == FALSE)
    {
        flags.buttonheld = TRUE;
        flags.active = (flags.active ? FALSE : TRUE);
    }
}

int32_t CONTROL_KeyboardFunctionPressed(int32_t which)
{
    if (CONTROL_CheckRange(which) || !CONTROL_Flags[which].used)
        return FALSE;

    int r = 0;
    auto &mapped = CONTROL_KeyMapping[which];

    if (mapped.keyPrimary != KEYUNDEFINED && !CONTROL_KeyBinds[mapped.keyPrimary].cmdstr)
        r = !!KB_KeyDown[mapped.keyPrimary];

    if (mapped.keySecondary != KEYUNDEFINED && !CONTROL_KeyBinds[mapped.keySecondary].cmdstr)
        r |= !!KB_KeyDown[mapped.keySecondary];

    return r;
}

#if 0
void CONTROL_ClearKeyboardFunction(int32_t which)
{
    if (CONTROL_CheckRange(which) || !CONTROL_Flags[which].used)
        return;

    auto &mapped = CONTROL_KeyMapping[which];

    if (mapped.key1 != KEYUNDEFINED)
        KB_KeyDown[mapped.key1] = 0;

    if (mapped.key2 != KEYUNDEFINED)
        KB_KeyDown[mapped.key2] = 0;
}
#endif

void CONTROL_DefineFlag(int which, int toggle)
{
    if (CONTROL_CheckRange(which)) return;

    controlflags &flags = CONTROL_Flags[which];

    flags.active     = FALSE;
    flags.buttonheld = FALSE;
    flags.cleared    = 0;
    flags.toggle     = toggle;
    flags.used       = TRUE;
}

int CONTROL_FlagActive(int which)
{
    if (CONTROL_CheckRange(which)) return FALSE;

    return CONTROL_Flags[which].used;
}

void CONTROL_MapKey(int32_t which, kb_scancode key1, kb_scancode key2)
{
    if (CONTROL_CheckRange(which)) return;

    CONTROL_KeyMapping[which].keyPrimary = key1 ? key1 : KEYUNDEFINED;
    CONTROL_KeyMapping[which].keySecondary = key2 ? key2 : KEYUNDEFINED;
}

#if 0
void CONTROL_PrintKeyMap(void)
{
    int32_t i;

    for (i=0; i<CONTROL_NUM_FLAGS; i++)
    {
        initprintf("function %2d key1=%3x key2=%3x\n",
                   i, CONTROL_KeyMapping[i].key1, CONTROL_KeyMapping[i].key2);
    }
}

void CONTROL_PrintControlFlag(int32_t which)
{
    initprintf("function %2d active=%d used=%d toggle=%d buttonheld=%d cleared=%d\n",
               which, CONTROL_Flags[which].active, CONTROL_Flags[which].used,
               CONTROL_Flags[which].toggle, CONTROL_Flags[which].buttonheld,
               CONTROL_Flags[which].cleared);
}

void CONTROL_PrintAxes(void)
{
    int32_t i;

    initprintf("numjoyaxes=%d\n", CONTROL_NumJoyAxes);
    for (i=0; i<CONTROL_NumJoyAxes; i++)
    {
        initprintf("axis=%d analog=%d digital1=%d digital2=%d\n",
                   i, CONTROL_JoyAxesMap[i].analogmap,
                   CONTROL_JoyAxesMap[i].minmap, CONTROL_JoyAxesMap[i].maxmap);
    }
}
#endif

void CONTROL_MapButton(int whichfunction, int whichbutton, int doubleclicked, controldevice device)
{
    controlbuttontype *set;

    if (CONTROL_CheckRange(whichfunction)) whichfunction = BUTTONUNDEFINED;

    switch (device)
    {
    case controldevice_mouse:
        if ((unsigned)whichbutton >= (unsigned)MAXMOUSEBUTTONS)
        {
            //Error("CONTROL_MapButton: button %d out of valid range for %d mouse buttons.",
            //		whichbutton, CONTROL_NumMouseButtons);
            return;
        }
        set = CONTROL_MouseButtonMapping;
        break;

    case controldevice_joystick:
        if ((unsigned)whichbutton >= (unsigned)MAXJOYBUTTONS)
        {
            //Error("CONTROL_MapButton: button %d out of valid range for %d joystick buttons.",
            //		whichbutton, CONTROL_NumJoyButtons);
            return;
        }
        set = CONTROL_JoyButtonMapping;
        break;

    default:
        //Error("CONTROL_MapButton: invalid controller device type");
        return;
    }

    if (doubleclicked)
        set[whichbutton].doubleclicked = whichfunction;
    else
        set[whichbutton].singleclicked = whichfunction;
}

void CONTROL_MapAnalogAxis(int whichaxis, int whichanalog, controldevice device)
{
    controlaxismaptype *set;

    if ((unsigned)whichanalog >= (unsigned)analog_maxtype && whichanalog != -1)
    {
        //Error("CONTROL_MapAnalogAxis: analog function %d out of valid range for %d analog functions.",
        //		whichanalog, analog_maxtype);
        return;
    }

    switch (device)
    {
    case controldevice_joystick:
        if ((unsigned)whichaxis >= (unsigned)MAXJOYAXES)
        {
            //Error("CONTROL_MapAnalogAxis: axis %d out of valid range for %d joystick axes.",
            //		whichaxis, MAXJOYAXES);
            return;
        }

        set = CONTROL_JoyAxesMap;
        break;

    default:
        //Error("CONTROL_MapAnalogAxis: invalid controller device type");
        return;
    }

    set[whichaxis].analogmap = whichanalog;
}

void CONTROL_SetAnalogAxisScale(int32_t whichaxis, int32_t axisscale, controldevice device)
{
    float *set;

    switch (device)
    {
    case controldevice_mouse:
        if ((unsigned) whichaxis >= ARRAY_SIZE(CONTROL_MouseAxesSensitivity))
            return;

        set = CONTROL_MouseAxesSensitivity;
        break;

    case controldevice_joystick:
        if ((unsigned) whichaxis >= (unsigned) MAXJOYAXES)
            return;

        set = CONTROL_JoyAxesSensitivity;
        break;

    default:
        return;
    }

    set[whichaxis] = (float)axisscale / 8192.f;
}

void CONTROL_SetAnalogAxisSensitivity(int32_t whichaxis, float axissens, controldevice device)
{
    float *set;

    switch (device)
    {
    case controldevice_mouse:
        if ((unsigned) whichaxis >= ARRAY_SIZE(CONTROL_MouseAxesSensitivity))
            return;

        set = CONTROL_MouseAxesSensitivity;
        break;

    case controldevice_joystick:
        if ((unsigned) whichaxis >= (unsigned) MAXJOYAXES)
            return;

        set = CONTROL_JoyAxesSensitivity;
        break;

    default:
        return;
    }

    set[whichaxis] = axissens;
}

void CONTROL_SetAnalogAxisInvert(int32_t whichaxis, int32_t invert, controldevice device)
{
    int8_t * set;

    switch (device)
    {
    case controldevice_joystick:
        if ((unsigned) whichaxis >= (unsigned) MAXJOYAXES)
        {
            //Error("CONTROL_SetAnalogAxisInvert: axis %d out of valid range for %d joystick axes.",
            //		whichaxis, MAXJOYAXES);
            return;
        }

        set = CONTROL_JoyAxesInvert;
        break;

    default:
        //Error("CONTROL_SetAnalogAxisInvert: invalid controller device type");
        return;
    }

    set[whichaxis] = invert;
}

void CONTROL_MapDigitalAxis(int32_t whichaxis, int32_t whichfunction, int32_t direction, controldevice device)
{
    controlaxismaptype *set;

    if (CONTROL_CheckRange(whichfunction)) whichfunction = AXISUNDEFINED;

    switch (device)
    {
    case controldevice_joystick:
        if ((unsigned) whichaxis >= (unsigned) MAXJOYAXES)
        {
            //Error("CONTROL_MapDigitalAxis: axis %d out of valid range for %d joystick axes.",
            //		whichaxis, MAXJOYAXES);
            return;
        }

        set = CONTROL_JoyAxesMap;
        break;

    default:
        //Error("CONTROL_MapDigitalAxis: invalid controller device type");
        return;
    }

    switch (direction)  	// JBF: this is all very much a guess. The ASM puzzles me.
    {
    case axis_up:
    case axis_left:
        set[whichaxis].minmap = whichfunction;
        break;
    case axis_down:
    case axis_right:
        set[whichaxis].maxmap = whichfunction;
        break;
    default:
        break;
    }
}

void CONTROL_ClearAssignments(void)
{
    memset(CONTROL_JoyAxes,             0,               sizeof(CONTROL_JoyAxes));
    memset(CONTROL_JoyAxesInvert,       0,               sizeof(CONTROL_JoyAxesInvert));
    memset(CONTROL_JoyAxesMap,          AXISUNDEFINED,   sizeof(CONTROL_JoyAxesMap));
    memset(CONTROL_JoyButtonMapping,    BUTTONUNDEFINED, sizeof(CONTROL_JoyButtonMapping));
    memset(CONTROL_KeyMapping,          KEYUNDEFINED,    sizeof(CONTROL_KeyMapping));
    memset(CONTROL_LastJoyAxes,         0,               sizeof(CONTROL_LastJoyAxes));
    memset(CONTROL_MouseButtonMapping,  BUTTONUNDEFINED, sizeof(CONTROL_MouseButtonMapping));

    for (auto & i : CONTROL_MouseAxesSensitivity)
        i = DEFAULTAXISSENSITIVITY;

    for (auto & i : CONTROL_JoyAxesSensitivity)
        i = DEFAULTAXISSENSITIVITY;
}

static int DoGetDeviceButtons(
    int32_t buttons, int32_t tm,
    int32_t NumButtons,
    int32_t *DeviceButtonState,
    int32_t *ButtonClickedTime,
    int32_t *ButtonClickedState,
    int32_t *ButtonClicked,
    uint8_t *ButtonClickedCount
)
{
    int32_t i=NumButtons-1;
    int retval = 0;

    for (; i>=0; i--)
    {
        int const bs = (buttons >> i) & 1;

        DeviceButtonState[i]  = bs;
        ButtonClickedState[i] = FALSE;

        if (bs)
        {
            retval = 1;

            if (ButtonClicked[i] == FALSE)
            {
                ButtonClicked[i] = TRUE;

                if (ButtonClickedCount[i] == 0 || tm > ButtonClickedTime[i])
                {
                    ButtonClickedTime[i]  = tm + CONTROL_DoubleClickSpeed;
                    ButtonClickedCount[i] = 1;
                }
                else if (tm < ButtonClickedTime[i])
                {
                    ButtonClickedState[i] = TRUE;
                    ButtonClickedTime[i]  = 0;
                    ButtonClickedCount[i] = 2;
                }
            }
            else if (ButtonClickedCount[i] == 2)
            {
                ButtonClickedState[i] = TRUE;
            }

            continue;
        }

        if (ButtonClickedCount[i] == 2)
            ButtonClickedCount[i] = 0;

        ButtonClicked[i] = FALSE;
    }

    return retval;
}

static void CONTROL_GetDeviceButtons(void)
{
    int32_t const t = ExtGetTime();

    if (CONTROL_MouseEnabled)
    {
        DoGetDeviceButtons(
            MOUSE_GetButtons(), t,
            CONTROL_NumMouseButtons,
            CONTROL_MouseButtonState,
            CONTROL_MouseButtonClickedTime,
            CONTROL_MouseButtonClickedState,
            CONTROL_MouseButtonClicked,
            CONTROL_MouseButtonClickedCount
        );
    }

    if (CONTROL_JoystickEnabled)
    {
        int retval = DoGetDeviceButtons(
            JOYSTICK_GetButtons(), t,
            CONTROL_NumJoyButtons,
            CONTROL_JoyButtonState,
            CONTROL_JoyButtonClickedTime,
            CONTROL_JoyButtonClickedState,
            CONTROL_JoyButtonClicked,
            CONTROL_JoyButtonClickedCount
        );
        if (retval)
            CONTROL_LastSeenInput = LastSeenInput::Joystick;
    }
}

static int CONTROL_DigitizeAxis(int axis, controldevice device)
{
    controlaxistype *set, *lastset;

    switch (device)
    {
    case controldevice_joystick:
        set = CONTROL_JoyAxes;
        lastset = CONTROL_LastJoyAxes;
        break;

    default: return 0;
    }

    set[axis].digitalCleared = lastset[axis].digitalCleared;

    if (set[axis].analog > 0)
    {
        if (set[axis].analog > DIGITALAXISANALOGTHRESHOLD || (set[axis].analog > MINDIGITALAXISANALOGTHRESHOLD && lastset[axis].digital == 1))
            set[axis].digital = 1;
        else
            set[axis].digitalCleared = 0;

        return 1;
    }
    else if (set[axis].analog < 0)
    {
        if (set[axis].analog < -DIGITALAXISANALOGTHRESHOLD || (set[axis].analog < -MINDIGITALAXISANALOGTHRESHOLD && lastset[axis].digital == -1))
            set[axis].digital = -1;
        else
            set[axis].digitalCleared = 0;

        return 1;
    }
    else
        set[axis].digitalCleared = 0;

    return 0;
}

static void CONTROL_ScaleAxis(int axis, controldevice device)
{
    controlaxistype *set;
    float *sens;
    int8_t * invert;

    switch (device)
    {
    case controldevice_joystick:
        set = CONTROL_JoyAxes;
        sens = CONTROL_JoyAxesSensitivity;
        invert = CONTROL_JoyAxesInvert;
        break;

    default: return;
    }

    int const invertResult = !!invert[axis];
    int const clamped = Blrintf(clamp<float>(set[axis].analog * sens[axis] * JOYSENSITIVITYMULTIPLIER, -MAXSCALEDCONTROLVALUE, MAXSCALEDCONTROLVALUE));
    set[axis].analog  = (clamped ^ -invertResult) + invertResult;
}

static void CONTROL_ApplyAxis(int axis, ControlInfo *info, controldevice device)
{
    controlaxistype *set;
    controlaxismaptype *map;

    switch (device)
    {
    case controldevice_joystick:
        set = CONTROL_JoyAxes;
        map = CONTROL_JoyAxesMap;
        break;

    default: return;
    }

    switch (map[axis].analogmap)
    {
    case analog_turning:          info->dyaw   += set[axis].analog; break;
    case analog_strafing:         info->dx     += set[axis].analog; break;
    case analog_lookingupanddown: info->dpitch += set[axis].analog; break;
    case analog_elevation:        info->dy     += set[axis].analog; break;
    case analog_rolling:          info->droll  += set[axis].analog; break;
    case analog_moving:           info->dz     += set[axis].analog; break;
    default: break;
    }
}

static inline int32_t joydist(int x, int y) { return ksqrt(x * x + y * y); }

static void CONTROL_PollDevices(ControlInfo *info)
{
    memset(info, 0, sizeof(ControlInfo));
    handleevents();

#ifdef __ANDROID__
    CONTROL_Android_PollDevices(info);
#endif

    if (CONTROL_MouseEnabled)
        CONTROL_GetMouseDelta(info);

    if (CONTROL_JoystickEnabled)
    {
        Bmemcpy(CONTROL_LastJoyAxes,   CONTROL_JoyAxes,   sizeof(CONTROL_JoyAxes));
        Bmemset(CONTROL_JoyAxes,   0, sizeof(CONTROL_JoyAxes));

        for (int i=joystick.numAxes-1; i>=0; i--)
        {
            int const input = joystick.pAxis[i];
            auto      axis  = &CONTROL_JoyAxes[i];
            int axisScaled10k = klabs(input * 10000 / 32767);

            if (axisScaled10k >= CONTROL_JoySaturation[i])
            {
                axis->analog = 32767 * ksgn(input);

                if (CONTROL_JoystickConsoleSpam)
                    OSD_Printf("controller axis %d saturated\n", i);
            }
            else 
            {
                // this assumes there are two sticks comprised of axes 0 and 1, and 2 and 3... because when isGameController is true, there are
                if (i <= CONTROLLER_AXIS_LEFTY || (joystick.isGameController && (i <= CONTROLLER_AXIS_RIGHTY)))
                    axisScaled10k = min(10000, joydist(joystick.pAxis[i & ~1], joystick.pAxis[i | 1]) * 10000 / 32767);

                if (axisScaled10k < CONTROL_JoyDeadZone[i])
                    axis->analog = 0;
                else
                {
                    axis->analog = input * (axisScaled10k - CONTROL_JoyDeadZone[i]) / CONTROL_JoySaturation[i];

                    if (CONTROL_JoystickConsoleSpam)
                        OSD_Printf("controller axis %d input %d scaled %d output %d\n", i, input, axisScaled10k, axis->analog);
                }
            }

            if (CONTROL_DigitizeAxis(i, controldevice_joystick))
                CONTROL_LastSeenInput = LastSeenInput::Joystick;

            CONTROL_ScaleAxis(i, controldevice_joystick);
            CONTROL_ApplyAxis(i, info, controldevice_joystick);
        }
    }

    CONTROL_GetDeviceButtons();
}

static int CONTROL_HandleAxisFunction(int32_t *p1, controlaxistype *axes, controlaxismaptype *axismap, int numAxes)
{
    int axis = numAxes - 1;
    int retval = 0;

    do
    {
        if (!axes[axis].digital)
            continue;

        int const j = (axes[axis].digital < 0) ? axismap[axis].minmap : axismap[axis].maxmap;

        if (j != AXISUNDEFINED)
        {
            p1[j] = 1;
            retval = 1;
        }
    }
    while (axis--);

    return retval;
}

static void CONTROL_AxisFunctionState(int32_t *p1)
{
    if (CONTROL_NumJoyAxes)
    {
        if (CONTROL_HandleAxisFunction(p1, CONTROL_JoyAxes, CONTROL_JoyAxesMap, CONTROL_NumJoyAxes))
            CONTROL_LastSeenInput = LastSeenInput::Joystick;
    }
}

static void CONTROL_ButtonFunctionState(int32_t *p1)
{
    if (CONTROL_NumMouseButtons)
    {
        int i = CONTROL_NumMouseButtons-1, j;

        do
        {
            if (!CONTROL_KeyBinds[MAXBOUNDKEYS + i].cmdstr)
            {
                j = CONTROL_MouseButtonMapping[i].doubleclicked;
                if (j != KEYUNDEFINED)
                    p1[j] |= CONTROL_MouseButtonClickedState[i];

                j = CONTROL_MouseButtonMapping[i].singleclicked;
                if (j != KEYUNDEFINED)
                    p1[j] |= CONTROL_MouseButtonState[i];
            }

            if (!CONTROL_BindsEnabled)
                continue;

            if (CONTROL_KeyBinds[MAXBOUNDKEYS + i].cmdstr && CONTROL_MouseButtonState[i])
            {
                if (CONTROL_KeyBinds[MAXBOUNDKEYS + i].repeat || (CONTROL_KeyBinds[MAXBOUNDKEYS + i].laststate == 0))
                    OSD_Dispatch(CONTROL_KeyBinds[MAXBOUNDKEYS + i].cmdstr);
            }
            CONTROL_KeyBinds[MAXBOUNDKEYS + i].laststate = CONTROL_MouseButtonState[i];
        }
        while (i--);
    }

    if (CONTROL_NumJoyButtons)
    {
        int i=CONTROL_NumJoyButtons-1, j;
        int retval = 0;

        do
        {
            j = CONTROL_JoyButtonMapping[i].doubleclicked;
            if (j != KEYUNDEFINED)
            {
                auto const state = CONTROL_JoyButtonClickedState[i];
                p1[j] |= state;
                retval |= state;
            }

            j = CONTROL_JoyButtonMapping[i].singleclicked;
            if (j != KEYUNDEFINED)
            {
                auto const state = CONTROL_JoyButtonState[i];
                p1[j] |= state;
                retval |= state;
            }
        }
        while (i--);

        if (retval)
            CONTROL_LastSeenInput = LastSeenInput::Joystick;
    }
}

void CONTROL_ClearButton(int whichbutton)
{
    if (CONTROL_CheckRange(whichbutton)) return;

#ifdef __ANDROID__
    CONTROL_Android_ClearButton(whichbutton);
#endif

    BUTTONCLEAR(whichbutton);
    CONTROL_Flags[whichbutton].cleared = TRUE;
}

void CONTROL_ClearAllButtons(void)
{
    CONTROL_ButtonHeldState = 0;
    CONTROL_ButtonState = 0;

    for (auto & c : CONTROL_Flags)
        c.cleared = TRUE;
}

int32_t CONTROL_GetControllerDigitalAxis(int32_t axis)
{
    if (!joystick.isGameController)
        return 0;

    return (CONTROL_JoyAxes[axis].digitalCleared || !CONTROL_JoyAxes[axis].digital) ? 0 : ksgn(CONTROL_JoyAxes[axis].digital);
}

void CONTROL_ClearControllerDigitalAxis(int32_t axis)
{
    if (!joystick.isGameController)
        return;

    CONTROL_JoyAxes[axis].digitalCleared = 1;
}

void CONTROL_ProcessBinds(void)
{
    if (!CONTROL_BindsEnabled)
        return;

    int i = MAXBOUNDKEYS-1;

    do
    {
        if (CONTROL_KeyBinds[i].cmdstr)
        {
            auto const keyPressed = KB_KeyPressed(i);

            if (keyPressed && (CONTROL_KeyBinds[i].repeat || (CONTROL_KeyBinds[i].laststate == 0)))
            {
                CONTROL_LastSeenInput = LastSeenInput::Keyboard;
                OSD_Dispatch(CONTROL_KeyBinds[i].cmdstr);
            }

            CONTROL_KeyBinds[i].laststate = keyPressed;
        }
    }
    while (i--);
}

static void CONTROL_GetFunctionInput(void)
{
    CONTROL_ButtonFunctionState(CONTROL_ButtonFlags);
    CONTROL_AxisFunctionState(CONTROL_ButtonFlags);

    CONTROL_ButtonHeldState = CONTROL_ButtonState;
    CONTROL_ButtonState = 0;

    int i = CONTROL_NUM_FLAGS-1;

    do
    {
        CONTROL_SetFlag(i, CONTROL_KeyboardFunctionPressed(i) | CONTROL_ButtonFlags[i]);

        if (CONTROL_Flags[i].cleared == FALSE) BUTTONSET(i, CONTROL_Flags[i].active);
        else if (CONTROL_Flags[i].active == FALSE) CONTROL_Flags[i].cleared = 0;
    }
    while (i--);

    memset(CONTROL_ButtonFlags, 0, sizeof(CONTROL_ButtonFlags));
}

void CONTROL_GetInput(ControlInfo *info)
{
#ifdef __ANDROID__
    CONTROL_Android_PollDevices(info);
#endif
    CONTROL_PollDevices(info);
    CONTROL_GetFunctionInput();
    inputchecked = 1;
}

static void CONTROL_ResetJoystickValues()
{
    CONTROL_NumJoyAxes      = min(MAXJOYAXES, joystick.numAxes);
    CONTROL_NumJoyButtons   = min(MAXJOYBUTTONS, joystick.numButtons + 4 * (joystick.numHats > 0));
    CONTROL_JoystickEnabled = CONTROL_JoyPresent = !!((inputdevices & DEV_JOYSTICK) >> 2);
}

void CONTROL_ScanForControllers()
{
    joyScanDevices();
    CONTROL_ResetJoystickValues();
}

bool CONTROL_Startup(controltype which, int32_t(*TimeFunction)(void), int32_t ticspersecond)
{
    UNREFERENCED_PARAMETER(which);

    if (CONTROL_Started) return false;

    ExtGetTime = TimeFunction ? TimeFunction : CONTROL_GetTime;

    // what the fuck???
    ticrate = ticspersecond;
    CONTROL_DoubleClickSpeed = (ticspersecond * 57) / 100;

    if (CONTROL_DoubleClickSpeed <= 0)
        CONTROL_DoubleClickSpeed = 1;

    if (initinput(CONTROL_ScanForControllers))
        return true;

    KB_Startup();

    CONTROL_NumMouseButtons = MAXMOUSEBUTTONS;
    CONTROL_MousePresent    = MOUSE_Startup();
    CONTROL_MouseEnabled    = CONTROL_MousePresent;

    CONTROL_ResetJoystickValues();

#ifdef GEKKO
    if (CONTROL_MousePresent)
        initprintf("CONTROL_Startup: Mouse Present\n");

    if (CONTROL_JoyPresent)
        initprintf("CONTROL_Startup: Joystick Present\n");
#endif

    CONTROL_ButtonState     = 0;
    CONTROL_ButtonHeldState = 0;

    for (auto & CONTROL_Flag : CONTROL_Flags)
        CONTROL_Flag.used = FALSE;

    CONTROL_Started = TRUE;

    return false;
}

void CONTROL_Shutdown(void)
{
    if (!CONTROL_Started)
        return;

    CONTROL_ClearAllBinds();

    MOUSE_Shutdown();
    uninitinput();

    CONTROL_Started = FALSE;
}

// temporary hack until input is unified

#define SCALEAXIS(x) (CONTROL_JoyAxes[CONTROLLER_AXIS_ ## x].analog * 10000 / 32767)
#define SATU(x) (CONTROL_JoySaturation[CONTROLLER_AXIS_ ## x])

UserInput *CONTROL_GetUserInput(UserInput *info)
{
    if (info == nullptr)
        info = &CONTROL_UserInput;

    direction newdir = dir_None;

    if ((CONTROL_JoyAxes[CONTROLLER_AXIS_LEFTY].digital == -1 && SCALEAXIS(LEFTY) <= -SATU(LEFTY))
        || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_DPAD_UP)))
        newdir = dir_Up;
    else if ((CONTROL_JoyAxes[CONTROLLER_AXIS_LEFTY].digital == 1 && SCALEAXIS(LEFTY) >= SATU(LEFTY))
                || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_DPAD_DOWN)))
        newdir = dir_Down;
    else if ((CONTROL_JoyAxes[CONTROLLER_AXIS_LEFTX].digital == -1 && SCALEAXIS(LEFTX) <= -SATU(LEFTX))
                || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_DPAD_LEFT)))
        newdir = dir_Left;
    else if ((CONTROL_JoyAxes[CONTROLLER_AXIS_LEFTX].digital == 1 && SCALEAXIS(LEFTX) >= SATU(LEFTX))
                || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_DPAD_RIGHT)))
        newdir = dir_Right;

    // allow the user to press the dpad as fast as they like without being rate limited
    if (newdir == dir_None)
    {
        CONTROL_UserInputDelay = -1;
        CONTROL_LastUserInputDirection = dir_None;
    }

    info->dir = (ExtGetTime() >= CONTROL_UserInputDelay) ? newdir : dir_None;

    if (KB_KeyDown[sc_kpad_8] || KB_KeyDown[sc_UpArrow])
        info->dir = dir_Up;
    else if (KB_KeyDown[sc_kpad_2] || KB_KeyDown[sc_DownArrow])
        info->dir = dir_Down;
    else if (KB_KeyDown[sc_kpad_4] || KB_KeyDown[sc_LeftArrow])
        info->dir = dir_Left;
    else if (KB_KeyDown[sc_kpad_6] || KB_KeyDown[sc_RightArrow])
        info->dir = dir_Right;

    info->b_advance = KB_KeyPressed(sc_Enter) || KB_KeyPressed(sc_kpad_Enter) || (MOUSE_GetButtons() & M_LEFTBUTTON)
                    || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_A));
    info->b_return   = KB_KeyPressed(sc_Escape) || (MOUSE_GetButtons() & M_RIGHTBUTTON) || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_B));
    info->b_escape = KB_KeyPressed(sc_Escape) || (JOYSTICK_GetControllerButtons() & (1 << CONTROLLER_BUTTON_START));

#if defined(GEKKO)
    if (JOYSTICK_GetButtons()&(WII_A))
        info->b_advance = true;

    if (JOYSTICK_GetButtons()&(WII_B|WII_HOME))
        info->b_return = true;

    if (JOYSTICK_GetButtons() & WII_HOME)
        info->b_escape = true;
#endif

    if (CONTROL_UserInputCleared[1])
    {
        if (!info->b_advance)
            CONTROL_UserInputCleared[1] = false;
        else
            info->b_advance = false;
    }

    if (CONTROL_UserInputCleared[2])
    {
        if (!info->b_return)
            CONTROL_UserInputCleared[2] = false;
        else
            info->b_return = false;
    }

    if (CONTROL_UserInputCleared[3])
    {
        if (!info->b_escape)
            CONTROL_UserInputCleared[3] = false;
        else
            info->b_escape = false;
    }

    return info;
}

#undef SCALEAXIS
#undef SATU

void CONTROL_ClearUserInput(UserInput * info)
{
    if (info == nullptr)
        info = &CONTROL_UserInput;

    // for keyboard keys we want the OS repeat rate, so just clear them
    KB_ClearKeyDown(sc_UpArrow);
    KB_ClearKeyDown(sc_kpad_8);
    KB_ClearKeyDown(sc_DownArrow);
    KB_ClearKeyDown(sc_kpad_2);
    KB_ClearKeyDown(sc_LeftArrow);
    KB_ClearKeyDown(sc_kpad_4);
    KB_ClearKeyDown(sc_RightArrow);
    KB_ClearKeyDown(sc_kpad_6);

    // the OS doesn't handle repeat for joystick inputs so we have to do it ourselves
    if (info->dir != dir_None)
    {
        auto const clk = ExtGetTime();

        if (CONTROL_LastUserInputDirection == info->dir)
            CONTROL_UserInputDelay = clk + ((ticrate * USERINPUTFASTDELAY) / 1000);
        else
        {
            CONTROL_LastUserInputDirection = info->dir;
            CONTROL_UserInputDelay = clk + ((ticrate * USERINPUTDELAY) / 1000);
        }

        CONTROL_UserInputCleared[0] = true;
    }

    if (info->b_advance)
    {
        KB_ClearKeyDown(sc_kpad_Enter);
        KB_ClearKeyDown(sc_Enter);
        MOUSE_ClearButton(M_LEFTBUTTON);
        CONTROL_UserInputCleared[1] = true;
    }

    if (info->b_return)
    {
        KB_ClearKeyDown(sc_Escape);
        MOUSE_ClearButton(M_RIGHTBUTTON);
        CONTROL_UserInputCleared[2] = true;
    }

    if (info->b_escape)
    {
        KB_ClearKeyDown(sc_Escape);
        CONTROL_UserInputCleared[3] = true;    
    }
    inputchecked = 1;
}
