After a fruitful conversation with [SkUaTeR](https://twitter.com/sanguinawer) few weeked ago, I discovered the wonderful world of [OISC](https://en.wikipedia.org/wiki/One_instruction_set_computer)s (One Instruction Set Computers).

OISCs refer to (usually virtual) machines whose instruction set architecture is composed by only one instruction. The cool feature about these machines is that they are  [Turing complete](https://en.wikipedia.org/wiki/Turing_completeness), and therefore can be programmed to become a universal computer.

The single-instruction architecture we discussed was based on the [RSSB](https://esolangs.org/wiki/RSSB) instruction and follows a [Von Neumann architecture](https://en.wikipedia.org/wiki/Von_Neumann_architecture) (this is, data and code belong to the same address space). <!--more--> Memory is word-addressed (e.g. 32 bit words) and both registers and I/O are memory mapped. In particular, the following addresses are reserved:

| Address | Contains |
|------------|-------------|
|`0x0`      | `$ip` (Instruction pointer) |
|`0x1`      | `$a` (Accumulator) |
|`0x2`     | `$0` (Null register, always 0) |
|`0x3`    | `$in` (Character input) |
|`0x4`   | `$out` (Character output) |

The `rssb` instructions always takes a memory location as operand, and behaves as follows: it subtracts the accumulator from the word pointed by the operand, and the result is stored both in the memory location and the accumulator. If the result of the subtraction was negative, the next instruction is skipped. **All words are unsigned, otherwise the borrow flag would not make any sense**. The following snippet in C summarizes the algorithm:

```c
void
rssb(unsigned long *operand)
{
  if (*operand < acc)
    ++ip;
  
  *operand -= acc;
  acc = *operand;
  ++ip;
}
```

The fact that the `rssb` has always the same format has an interesting consequence. Since there is only one instruction, the minimum number of bits we need to encode the opcode of this instruction is $$N_{opcode}=\left\lceil{log_2(\text{nr. of instructions})}\right\rceil=\left\lceil{log_2(1)}\right\rceil=0$$

And therefore the only information we need to enconde is the operand of each `rssb` instruction. For example, the following instructions:

```
   rssb 0x342a
   rssb $0
   rssb $a
   rssb $out
```
Will be enconded as:
```php
   0x342a, 0x0002, 0x0001, 0x0004
```
Implying that the same `rssb` instruction can be used to define both code and data. Pretty simple, isn't it? Let's see how we can do some useful computation with it :)

## Basic operations
This will be just a summary of how most common modern assembly instructions can be implemented in RSSB. These implementations are not necessarily optimal, but they illustrate the incremental nature of OISC programming. All the code presented here can be compiled through a small RSSB emulator I wrote a few days ago that can be downloaded from [my personal GitHub account](https://github.com/BatchDrake/rssb).

Maybe the most basic operation of all of them is **clearing the accumulator**. This can be simply done by:

```php
.macro ZERO
    rssb $a
.end
```

Since we are subtracting `$a` from itself, its contents will become zero. Also, the result is non-negative, so the next instruction is not skipped. We introduce the keyword `.macro` to show how the previous code can be used to produce more and more complex behaviors. For example, to **clear arbitrary memory addresses**:

```php
.macro CLEAR ADDR
    ZERO
    rssb ADDR
    rssb ADDR
.end
```

Which just subtracts the memory address from itself, clearing both the address and the accumulator. Some alternatives to this approach can be found in the Internet:

```php
.macro CLEAR ADDR
    rssb ADDR
    rssb ADDR # Conditionally executed
    rssb ADDR
.end
```
Another interesting macro we can define here is `INVERT`, used to **invert the accumulator sign**, with no side effects:

```
.macro INVERT
    rssb $0    # $new_a = 0 - $a
    rssb $a    # This line is skipped if $a != 0. Otherwise, $a is still 0.
.end
```

 **Retrieving a memory word** and placing it in  the accumulator is also easy if we ensure the accumulator is cleared first:

```php
.macro GET ADDR
    rssb $a
    rssb ADDR
.end
```

Or, using macros:

```php
.macro GET ADDR
    ZERO
    rssb ADDR
.end
```

Which is slightly faster, as the second instruction is executed only if the accumulator was lesser than the contents of `ADDR` before the first instruction. 

**To load an immediate NONZERO value** (or the address of a label) we leverage that `rssb` instructions can be used to define data:

```php
.macro IMM VALUE
  GET OFFSET # In $a: address of LABEL (nonzero in general)
  rssb $0    # Invert $a. As $a is nonzero, the next instruction is skipped
OFFSET:
  rssb VALUE # This memory position contains the address of LABEL
  rssb $0    # Invert $a again to its original sign.
  ZERO       # This instruction is always skipped as VALUE was nonzero
.end
```

**Storing information** is a bit trickier. We need a special memory location that must be cleared on initialization:
```php
_start:
  CLEAR TMP
```

And we use that location as a temporary storage, that must be cleared after usage:
```php
.macro STORE ADDR
  rssb TMP      # Stores -$old_a in TMP
  rssb $a       # Only executed if $a was 0
  CLEAR ADDR    # *ADDR = $a = 0x0
  rssb TMP      # $a = -$old_a
  rssb ADDR     # *ADDR = $old_a
  CLEAR TMP     # Leave TMP cleared
.end
```

Retrieving and clearing stuff sure is great, but we can definitely do more interesting things. How about **moving memory**? It can be as easy as:

```php
.macro MOVE DEST ORIGIN
  GET ORIGIN
  STORE DEST # Note this leaves $a = 0
.end
```

Before doing more interesting things, it is a good idea to have a debug macro to print some characters to the console. As mentioned above, this is attained by writing on `$out`. This special memory region is designed in a way it behaves like `$0`, so that everything we write there is printed out and lost forever. Given this definition, we can define the macro `PUTCHAR` to print the least significant byte of the contents of the memory address passed as argument:

```
.macro PUTCHAR ADDR
  ZERO             # $a = 0
  rssb ADDR        # $a = *addr
  INVERT
  rssb $out        # 0 - $a = 0 - (-*addr) = *addr
  rssb $a          # Skipped if *addr != 0
.end
```

We have to stress the fact that the behavior of `$out` is not well defined in the RSSB architecture. In the VM I've written I could have kept a copy of the last printed character. In that case, I would need to retrieve the last printed character in `$out` to subtract `$out - NEW_CHAR` from it (resulting in `NEW_CHAR`).

## Increasing complexity
Now that we can move memory and print its contents, we can go further and so some basic math. Subtracting two operands and placing the result somewhere else can be done like this:

```
.macro SUB RES OP1 OP2
  MOVE RES, OP1   # Move OP1 to RES. $a = 0
  rssb OP2        # Put OP2 in $a. OP2 untouched
  rssb RES        # RES: OP1 - OP2
  rssb $a         # This is potentially skipped
.end
```

Addition can be seen as a subtraction after inverting `OP2`'s sign:

```
.macro ADD RES OP1 OP2
  MOVE RES, OP1   # Move OP1 to RES. $a = 0
  rssb OP2        # Put OP2 in $a. OP2 untouched
  INVERT          # Invert OP2
  rssb RES        # RES: OP1 - (-OP2) = OP1 + OP2
  rssb $a         # This is potentially skipped
.end
```

Another useful macro we can define is `JUMP`, used to perform branching. Unconditional jumps can be done by altering the `$ip` register. However, since altering `$ip` will immediately take us somewhere else, we have to do it right in a single `rssb $ip` instruction.

How to do this? First, let's take a look to what `rssb $ip` does:

1. `$a > $ip`? In that case, there is borrow.
2. `$ip -= $a`: Set `$ip` `$a` instructions behind.
3. `++$ip`: The next instruction is `$a-1` positions behind.
4. There was borrow? `$++ip` and the next instruction is actually `$a-2` positions behind.

Therefore, if we want to jump to an arbitrary position, we have to put `RSSB_IP-LABEL+1` in it first. If we use `%` to refer to the current `$ip` of the assembled instruction, the jump instruction takes the form:

```php
.macro JUMP LABEL
  GET LOOPOFF  # LOOPOFF is right after `rssb $ip`
  rssb $ip
LOOPOFF:
  rssb %-LABEL   # Distance from last LOOP label
.end
```

And sice the instruction at `LOOPOFF` is right after `rssb $ip`(located at `RSSB_IP`), the instruction assembles to `RSSB_IP-LABEL+1`.

There is something to take into account, though. It may happen that `rssb $ip` causes borrow, and may skip the first instruction at `LABEL`. In particular, this happens when the address of `LABEL` is *after* the `JUMP` instruction. In those cases, we must add a dummy instruction (e.g. `ZERO`) right after the jump target label:

```php
  JUMP FORWARD # Forward jump
#
# Some code
#
FORWARD:
  ZERO
  # Executed code starts here
```

## Comparisons and conditional branching
We cannot do useful programming without comparisons and conditional branching, therefore we need first some comparison instruction. The following macro can be used to detect if `A < B` (and, therefore, if `A >= B`). If `A < B`, `CMP` will put `1` in the accumulator. Otherwise, it will leave it in `0`:

```php
.macro CMP A B
  MOVE COPY, A 
  GET B
  rssb COPY             # copy = $a = A - B
  rssb $a               # Skipped if A < B
  STORE COPY
  ####### At this point: $a = A < B ? (negative diff) : 0 #######
  
  SUB COPY2, ONE, COPY  # COPY2 = 1 - COPY
  
  GET COPY2             # Restore conditional expression
  rssb $0               # Attempt to invert
  rssb ONE              # It was zero? $a = 1. Next instruction will make this zero.
  rssb COPY             # It was nonzero? COPY - (-COPY2) = COPY - (COPY - 1) = 1
.end

####### This macro requires the following labels to be defined ###########
COPY:
  rssb 0
COPY2:
  rssb 0
ONE:
  rssb 1
```

Conditional branching can be implemented on top of this instruction, using its result to compute the number of instructions that must be skipped if the condition is true (in particular, we use it to skip the `JUMP`). Note that this implies that the instruction jumps to the target `LABEL` if the condition is **false**:

```php
.macro JGE A B LABEL # Jumps to label if A >= B
  CLEAR SKIP         # Set skip to 0
  CMP A, B           # Returns 1 if A < B, 0 otherwise
  STORE COPY         # Store comparison result (can be either 1 or 0)
  
  # Now we use the comparison result to compute the number of instructions
  # to skip. SKIP = 4 * comparison_result (4 or 0)
  
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  
  GET SKIP             # Get skip length
  INVERT               # Make it negative
  rssb $ip             # If $a = 0: no skip. Otherwise: skip 4 instructions.
  
  JUMP LABEL           # Not skipped (JUMP is 4 instructions long)
  
  ZERO
.end

####### This macro requires the following labels to be defined ###########
SKIP:
  rssb 0
  
```

Adapting `JGE` to other conditions is trivial, boiling down to replace `CMP` by an appropriate testing macro that stores `1` or `0` in the accumulator.

## Pointer madness
Dealing with pointers in RSSB is extremely fun because it involves self-modifying code. In particular, pointer de-referencing is performed by altering an RSSB instruction to recover the word at the pointed address. The following macro would be equivalent to `$a = *(*ADDR)` in C.

```php
.macro GET_PTR ADDR
  MOVE DO_GET, ADDR    # Alter instruction to retrieve given value
  ZERO 	       	       # Clear accumulator
DO_GET:
  rssb 0               # Modified by previous MOVE: $a = *(*ADDR) - 0
.end
```

Storing data through a pointer follows the same philosophy, requiring to keep a copy of the data in `TMP` first. The following macro performs the equivalent of `*(*ADDR) = $a` in C:

```php
.macro STORE_PTR ADDR
  rssb TMP      # Save $a
  rssb $a       # Only executed if $a was 0

  MOVE DO_CLEAR, ADDR # Alter clear   instruction
  MOVE DO_STORE, ADDR # Alter storage instruction

DO_CLEAR:
  CLEAR 0       # *(*ADDR) = $a = 0x0
  rssb TMP      # $a = -$old_a

DO_STORE:
  rssb 0        # *(*ADDR) = $old_a
  CLEAR TMP     # Leave TMP cleared. $a IS NOW 0
.end
```

## A full-featured Hello World, with macros
There is something we have not discussed yet though, and it is how to stop the VM. We will use the following seemingly nonsensical macro:

```php
.macro EXIT
  ZERO           # Clear accumulator
  rssb $ip       # $ip = $a= SOMETHING, jump next.
  rssb $ip       # $ip = $a = SOMETHING + 1 - SOMETHING = 1
  # $a = 1 and $ip is now 2
.end
```
This sequence of instructions causes the accumulator to become 1 and the `$ip` to become 2. We detect this condition in the RSSB VM to stop the program execution.

Now we have all the tools to code the classical Hello World, using macros. Now it is not that difficult, right? ;)

```php
################################# ENTRY POINT #################################

LOOP:
  GET_PTR PTR         # Read character pointed by PTR
  STORE CHAR          # Save it in variable CHAR

  JGE $0, CHAR, END   # Is CHAR lesser or equal than zero? If yes, end.
  
  PUTCHAR CHAR        # Otherwise, put character

  ADD PTR, PTR, ONE   # Increase string pointer
  JUMP LOOP           # Repeat!
  
END:
  ZERO                # We reach this place from a forward jump
  EXIT                # Exit VM

################################ PROGRAM DATA #################################
PTR:
  rssb HELLO # Address of first char of the string

ONE2:
  rssb 1
CHAR:
  rssb 0
  
HELLO:
  rssb 'H'
  rssb 'e'
  rssb 'l'
  rssb 'l'
  rssb 'o'
  rssb ' '
  rssb 'w'
  rssb 'o'
  rssb 'r'
  rssb 'l'
  rssb 'd'
  rssb ','
  rssb ' '
  rssb 'w'
  rssb 'i'
  rssb 't'
  rssb 'h'
  rssb ' '
  rssb 'm'
  rssb 'a'
  rssb 'c'
  rssb 'r'
  rssb 'o'
  rssb 's'
  rssb '!'
  rssb 0x0a
  rssb 0
  
############################## MACRO DEFINITIONS ##############################
.macro ZERO
    rssb $a
.end

.macro GET ADDR
    ZERO
    rssb ADDR
.end

.macro CLEAR ADDR
    ZERO
    rssb ADDR
    rssb ADDR
    rssb ADDR
.end
  
.macro STORE ADDR
  rssb TMP      # Save $a
  rssb $a       # Only executed if $a was 0
  CLEAR ADDR    # *ADDR = $a = 0x0
  rssb TMP      # $a = -$old_a
  rssb ADDR     # *ADDR = $old_a
  CLEAR TMP     # Leave TMP cleared. $a IS NOW 0
.end

.macro MOVE DEST ORIGIN
  GET ORIGIN
  STORE DEST
.end

.macro PUTCHAR ADDR
  ZERO             # $a = 0
  rssb ADDR        # $a = *addr
  INVERT
  rssb $out        # 0 - $a = 0 - (-*addr) = *addr
  rssb $a          # Skipped if *addr != 0
.end

.macro EXIT
  ZERO             # Clear accumulator
  rssb $ip
  rssb $ip
.end

.macro INVERT
  rssb $0
  rssb $a
.end

.macro ADD RES OP1 OP2
  MOVE RES, OP1   # Move OP1 to RES. $a = 0
  rssb OP2        # Put OP2 in $a. OP2 untouched
  INVERT
  rssb RES        # RES: OP1 - (-OP2) = OP1 + OP2
  rssb $a         ############ Potentially skipped ###############
.end

.macro SUB RES OP1 OP2
  MOVE RES, OP1   # Move OP1 to RES. $a = 0
  rssb OP2        # Put OP2 in $a. OP2 untouched
  rssb RES        # RES: OP1 - OP2
  rssb $a         ############# Potentially skipped #############
.end

.macro JUMP LABEL
  GET LOOPOFF
  rssb $ip
LOOPOFF:
  rssb %-LABEL   # Distance from last LOOP label
.end

.macro CMP A B
  MOVE COPY, A 
  GET B
  rssb COPY             # copy = $a = A - B
  rssb $a               # Skipped if B > A
  STORE COPY
  ####### At this point: $a = B > A ? (negative diff) : 0 #######
  
  SUB COPY2, ONE, COPY  # Copy2: COPY + 1
  
  GET COPY2             # Restore conditional expression
  rssb $0               # Attempt to invert
  rssb ONE              # It was zero? $a = 1. Leaves it untouched, skips next                           
  rssb COPY             # It was nonzero?
.end

.macro JGE A B LABEL
  CLEAR SKIP
  CMP A, B
  STORE COPY
  
  # Now we use the comparison result to compute the number of instructions
  # to skip
  
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  ADD SKIP, SKIP, COPY
  
  GET SKIP
  INVERT               # Make it negative
  
  rssb $ip             # If $a = 0: no skip. Otherwise: skip 4 instructions.
  
  JUMP LABEL           # JUMP is 4 instructions long 
  
  ZERO
.end

.macro GET_PTR ADDR
  MOVE DO_GET, ADDR    # Alter instruction to retrieve given value
  ZERO                 # Clear accumulator
DO_GET:
  rssb 0               # Modified by pervious MOVE
.end

.macro STORE_PTR ADDR
  rssb TMP      # Save $a
  rssb $a       # Only executed if $a was 0

  MOVE DO_CLEAR, ADDR # Alter clear   instruction
  MOVE DO_STORE, ADDR # Alter storage instruction

DO_CLEAR:
  CLEAR 0       # *(*ADDR) = $a = 0x0
  rssb TMP      # $a = -$old_a

DO_STORE:
  rssb 0        # *(*ADDR) = $old_a
  CLEAR TMP     # Leave TMP cleared. $a IS NOW 0
.end

.macro IMM VALUE
  GET OFFSET # In $a: address of VALUE (nonzero in general)
  rssb $0    # Invert $a. As $a is nonzero, the next instruction is skipped
OFFSET:
  rssb VALUE # This memory position contains the address of VALUE
  rssb $0    # Invert $a again to its original sign.
  ZERO       # This instruction is skipped if VALUE was nonzero
.end

############################## TEMPORARY STORAGE ##############################
TMP:
  rssb 0

COPY:
  rssb 0

COPY2:
  rssb 0
  
SKIP:
  rssb 0
  
################################### .rodata ###################################
ONE: 
  rssb 1

  
```

And that's all what it takes! Note that we could make the program smaller (in the sense of having less instructions) by doing some unusual operations (like counting the accumulative value of characters written). However, although this is technically faster, it would not be a good example on how to make a practical use of this architecture.

There are plenty of OISC architectures, and I may come back to them in the future with similar examples. Stay tuned!
