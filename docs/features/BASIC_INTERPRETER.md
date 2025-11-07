# BASIC Interpreter Documentation

## Overview

A minimal BASIC interpreter integrated into the x86 microkernel, providing an interactive programming environment directly from the kernel shell.

## Features

- **100 program lines** (numbered 0-99)
- **64 variables** for data storage
- **16-level subroutine stack** for GOSUB/RET
- **Expression evaluation** with multiple operators
- **Interactive mode** with line editing and history
- **Persistent programs** during interpreter session

## Getting Started

### Launching the Interpreter

From the kernel shell, type:
```
> basic
```

The interpreter will display:
```
Starting BASIC interpreter...
Type 'EXIT' to return to shell

BASIC Interpreter v1.0
Enter program (type 'RUN' to execute):

] 
```

### Your First Program

```basic
] 10 PRINT "HELLO WORLD"
] 20 END
] RUN

Running program...
HELLO WORLD

Program ended.
```

## Language Reference

### Commands

#### PRINT
Outputs text strings and variable values.

**Syntax:**
```basic
PRINT "text" [variable] ["more text"] [expression]
```

**Examples:**
```basic
10 PRINT "HELLO"
20 PRINT X
30 PRINT "VALUE: " X
40 PRINT X+Y
```

**Notes:**
- Strings must be enclosed in double quotes
- Multiple items can be printed on one line
- Automatically adds newline at end

#### INPUT
Reads user input into a variable.

**Syntax:**
```basic
INPUT variable
```

**Example:**
```basic
10 INPUT AGE
20 PRINT "YOU ARE " AGE " YEARS OLD"
```

**Notes:**
- Prompts with variable name followed by `?`
- Accepts numeric input only
- Non-numeric input will be interpreted as 0

#### VAR
Assigns a value to a variable.

**Syntax:**
```basic
VAR variable expression
```

**Examples:**
```basic
10 VAR X 5
20 VAR Y X+10
30 VAR Z X*Y
```

**Notes:**
- Variable names can be alphanumeric
- Up to 7 characters per variable name
- Case-sensitive

#### IF
Conditional execution of a command.

**Syntax:**
```basic
IF condition command
```

**Examples:**
```basic
10 IF X<10 PRINT "LESS THAN 10"
20 IF X=Y GOTO 50
30 IF A>B VAR MAX A
```

**Notes:**
- Only executes one command
- Condition must be a comparison expression
- 0 = false, non-zero = true

#### GOTO
Jumps to a specific line number.

**Syntax:**
```basic
GOTO linenum
```

**Examples:**
```basic
10 VAR X 0
20 PRINT X
30 VAR X X+1
40 IF X<10 GOTO 20
50 END
```

**Notes:**
- Line number must exist in program
- Out-of-range line numbers are ignored

#### GOSUB / RET
Subroutine call and return.

**Syntax:**
```basic
GOSUB linenum
RET
```

**Example:**
```basic
10 VAR X 5
20 GOSUB 100
30 PRINT "DONE"
40 END
100 PRINT "SUBROUTINE: " X
110 RET
```

**Notes:**
- Maximum 16 nested GOSUB calls
- RET returns to line after GOSUB
- Stack overflow will cause errors

#### END
Terminates program execution.

**Syntax:**
```basic
END
```

**Example:**
```basic
10 PRINT "STARTING"
20 IF X<0 END
30 PRINT "POSITIVE"
40 END
```

### Operators

#### Arithmetic Operators
| Operator | Operation      | Example | Result |
|----------|---------------|---------|--------|
| `+`      | Addition      | `5+3`   | 8      |
| `-`      | Subtraction   | `5-3`   | 2      |
| `*`      | Multiplication| `5*3`   | 15     |
| `/`      | Division      | `15/3`  | 5      |
| `%`      | Modulo        | `5%3`   | 2      |

#### Comparison Operators
| Operator | Operation      | Example | Result |
|----------|---------------|---------|--------|
| `=`      | Equal         | `5=5`   | 1      |
| `~`      | Not Equal     | `5~3`   | 1      |
| `<`      | Less Than     | `3<5`   | 1      |
| `>`      | Greater Than  | `5>3`   | 1      |

#### Bitwise Operators
| Operator | Operation     | Example | Result |
|----------|--------------|---------|--------|
| `&`      | Bitwise AND  | `5&3`   | 1      |
| `|`      | Bitwise OR   | `5|3`   | 7      |

**Operator Precedence:**
Operators are evaluated left-to-right based on character appearance in expression. Use nested expressions for complex operations.

### Variables

- **Names:** Alphanumeric, up to 7 characters
- **Type:** Integer only (32-bit signed)
- **Range:** -2,147,483,648 to 2,147,483,647
- **Capacity:** Maximum 64 variables per session
- **Initialization:** Variables default to 0 if not set

**Valid variable names:**
```basic
X, Y, Z, A1, B2, COUNT, TOTAL, NUM
```

### Interpreter Commands

These commands are entered at the `]` prompt:

#### RUN
Executes the current program.
```
] RUN
```

#### LIST
Displays all program lines.
```
] LIST

Program listing:
10 PRINT "HELLO"
20 END
```

#### NEW
Clears the current program and all variables.
```
] NEW
Program cleared.
```

#### EXIT / QUIT
Returns to the kernel shell.
```
] EXIT

BASIC interpreter exited.
>
```

## Example Programs

### 1. Hello World
```basic
10 PRINT "HELLO WORLD"
20 END
```

### 2. Counter Loop
```basic
10 VAR X 0
20 PRINT "COUNT: " X
30 VAR X X+1
40 IF X<10 GOTO 20
50 END
```
**Output:**
```
COUNT: 0
COUNT: 1
COUNT: 2
...
COUNT: 9
```

### 3. User Input
```basic
10 INPUT NAME
20 INPUT AGE
30 PRINT "HELLO, USER"
40 PRINT "YOU ARE " AGE " YEARS OLD"
50 END
```

### 4. Addition Calculator
```basic
10 PRINT "ENTER TWO NUMBERS"
20 INPUT A
30 INPUT B
40 VAR SUM A+B
50 PRINT "SUM: " SUM
60 END
```

### 5. Multiplication Table
```basic
10 INPUT N
20 VAR I 1
30 VAR R N*I
40 PRINT N " X " I " = " R
50 VAR I I+1
60 IF I<11 GOTO 30
70 END
```

### 6. Factorial (with GOSUB)
```basic
10 INPUT N
20 VAR RESULT 1
30 VAR I N
40 GOSUB 100
50 PRINT "FACTORIAL: " RESULT
60 END
100 IF I<2 RET
110 VAR RESULT RESULT*I
120 VAR I I-1
130 GOTO 100
```

### 7. Find Maximum
```basic
10 INPUT A
20 INPUT B
30 IF A>B VAR MAX A
40 IF B>A VAR MAX B
50 IF A=B VAR MAX A
60 PRINT "MAXIMUM: " MAX
70 END
```

### 8. Odd/Even Checker
```basic
10 INPUT NUM
20 VAR REM NUM%2
30 IF REM=0 PRINT "EVEN"
40 IF REM=1 PRINT "ODD"
50 END
```

### 9. Countdown Timer
```basic
10 VAR COUNT 10
20 PRINT "COUNTDOWN: " COUNT
30 VAR COUNT COUNT-1
40 IF COUNT>0 GOTO 20
50 PRINT "LIFTOFF!"
60 END
```

### 10. Number Guessing Game
```basic
10 VAR SECRET 42
20 PRINT "GUESS THE NUMBER (1-100)"
30 INPUT GUESS
40 IF GUESS=SECRET GOTO 100
50 IF GUESS<SECRET PRINT "TOO LOW"
60 IF GUESS>SECRET PRINT "TOO HIGH"
70 GOTO 30
100 PRINT "CORRECT! YOU WIN!"
110 END
```

## Technical Details

### Memory Limits
- **Program Storage:** 100 lines × 64 bytes = 6.4 KB
- **Variable Storage:** 64 variables × (8 bytes name + 4 bytes value) = 768 bytes
- **GOSUB Stack:** 16 levels × 4 bytes = 64 bytes
- **Total Memory:** ~7.2 KB

### Expression Evaluation
Expressions are evaluated using a recursive descent parser:
1. Scan for operators from left to right
2. Split expression at operator
3. Recursively evaluate left and right sides
4. Apply operator to results

**Example:** `5+3*2` evaluates as `(5+3)*2 = 16` (not `5+(3*2) = 11`)

### Line Number Management
- Lines are stored in an array indexed by line number
- Empty lines (line number with no command) delete that line
- Line numbers can be entered in any order
- Execution proceeds sequentially unless GOTO/GOSUB used

### Error Handling
The interpreter reports errors with line numbers when possible:
```
ERROR AT 20: INVALID COMMAND
ERROR: INVALID ARGS
```

Common errors:
- **INVALID COMMAND:** Unknown command
- **INVALID ARGS:** Wrong number/type of arguments
- **INVALID IF STATEMENT:** Malformed IF condition
- **INVALID GOTO/GOSUB:** Missing line number
- **PARSER: MISSING NUMBER:** Line doesn't start with number

## Limitations

1. **No string variables** - Only numeric variables supported
2. **No arrays** - Individual variables only
3. **No FOR loops** - Use IF/GOTO instead
4. **No floating point** - Integer arithmetic only
5. **Limited lines** - Maximum 100 program lines
6. **No file I/O** - Programs not saved between sessions
7. **No ELSE clause** - Use separate IF statements
8. **Simple precedence** - Operators evaluated left-to-right

## Tips and Tricks

### 1. Simulating FOR loops
```basic
10 VAR I 1
20 PRINT I
30 VAR I I+1
40 IF I<11 GOTO 20
```

### 2. Boolean logic with variables
```basic
10 VAR TRUE 1
20 VAR FALSE 0
30 IF TRUE PRINT "YES"
40 IF FALSE PRINT "NO"
```

### 3. Multiple conditions
```basic
10 VAR OK 0
20 IF X>5 VAR OK 1
30 IF X<10 VAR OK OK&1
40 IF OK PRINT "X IS BETWEEN 5 AND 10"
```

### 4. Using modulo for patterns
```basic
10 VAR X 0
20 VAR REM X%3
30 IF REM=0 PRINT "FIZZ"
40 VAR X X+1
50 IF X<15 GOTO 20
```

### 5. Creating delays
```basic
10 VAR I 0
20 VAR I I+1
30 IF I<10000 GOTO 20
40 PRINT "DONE"
```

### 6. Line number gaps for insertions
Use gaps between line numbers (10, 20, 30) so you can insert lines later:
```basic
10 PRINT "START"
20 PRINT "END"
```
Later insert:
```basic
15 PRINT "MIDDLE"
```

## Troubleshooting

### Program doesn't run
- Check that all lines start with a number
- Ensure program has an END statement
- Verify line numbers are 0-99

### Variables show unexpected values
- Variables persist between RUNs - use NEW to clear
- Check for typos in variable names (case-sensitive)
- Verify variable doesn't exceed 7 characters

### GOSUB/RET errors
- Ensure every GOSUB has a matching RET
- Check for stack overflow (max 16 levels)
- Verify subroutine line numbers exist

### Expression errors
- Remember left-to-right evaluation
- Use separate VAR statements for complex expressions
- Check for division by zero

## Integration with Kernel

The BASIC interpreter is compiled directly into the kernel and accessed via the `basic` shell command. It uses:

- **`printf()`** for output
- **`getchar()`** for character input  
- **`get_input_line()`** for line input
- **Kernel memory allocator** for variables and program storage

All I/O is compatible with both VGA text mode and serial console (nographic mode).

## Future Enhancements

Potential features for future versions:
- [ ] String variables and string operations
- [ ] Arrays with dimension statements
- [ ] FOR/NEXT loops
- [ ] File I/O (LOAD/SAVE programs)
- [ ] More mathematical functions (SIN, COS, SQR)
- [ ] RANDOM number generator
- [ ] Extended line limit (200+ lines)
- [ ] Floating-point arithmetic
- [ ] Multi-line IF/THEN/ELSE
- [ ] Error line number tracking
- [ ] Breakpoints and debugging

## Version History

### v1.0 (Current)
- Initial release
- Basic command set (PRINT, INPUT, VAR, IF, GOTO, GOSUB, RET, END)
- Expression evaluation
- 100 lines, 64 variables
- Interactive mode with RUN, LIST, NEW commands

## License

Integrated into the x86 microkernel project.

## Credits

Based on a minimal BASIC interpreter design, adapted for bare-metal kernel integration with serial console support.
