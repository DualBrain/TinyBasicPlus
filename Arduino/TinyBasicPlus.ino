// TinyBASIC.cpp : Defines the entry point for the console application.
// Author: Mike Field <hamster@snap.net.nz>
//


// v0.02b: 2012-09-17  Scott Lawrence <yorgle@gmail.com>
//         Better FILES listings
//
// v0.02a: 2012-09-17  Scott Lawrence <yorgle@gmail.com>
//         Support for SD Library
//         Added: SAVE, FILES (mostly works), LOAD (mostly works) (redirects IO)
//         Added: MEM, ? (PRINT)
//         Quirk:  "10 LET A=B+C" is ok "10 LET A = B + C" is not.
//         Quirk:  INPUT seems broken?



// IF testing with Visual C 
//#include "stdafx.h"
char eliminateCompileErrors = 1; 

#define ARDUINO 1

// Feature configuration...

// This enables LOAD, SAVE, FILES commands through the Arduino SD Library
// it adds 9k of usage as well.
#define ENABLE_FILEIO 1



#if ARDUINO
  // includes for Arduino functionality
  #if ENABLE_FILEIO
  #include <SD.h>

  // Arduino-specific configuration
  // set this to the card select for your SD shield
  #define kSD_CS 10

  #define kSD_Fail  0
  #define kSD_OK    1
  #endif

  // size of our program ram
  #define kRamSize   412
  
  // for file writing
  #if ENABLE_FILEIO
  File fp;
  static boolean sd_is_initialized = false;
  static boolean outToFile = false;
  static boolean inFromFile = false;
  #endif


#else
  // Not arduino setup

  // size of our program ram
  #define kRamSize   4096

#endif

// ASCII Characters
#define CR	'\r'
#define NL	'\n'
#define TAB	'\t'
#define BELL	'\b'
#define SPACE   ' '
#define SQUOTE  '\''
#define DQUOTE  '\"'
#define CTRLC	0x03
#define CTRLH	0x08
#define CTRLS	0x13
#define CTRLX	0x18

typedef short unsigned LINENUM;
#if ARDUINO
  #define ECHO_CHARS 1
#else
  #define ECHO_CHARS 0
#endif


static unsigned char program[kRamSize];
static unsigned char *txtpos,*list_line;
static unsigned char expression_error;
static unsigned char *tempsp;

/***********************************************************/
// Keyword table and constants - the last character has 0x80 added to it
static unsigned char keywords[] = {
	'L','I','S','T'+0x80,
	'L','O','A','D'+0x80,
	'N','E','W'+0x80,
	'R','U','N'+0x80,
	'S','A','V','E'+0x80,
	'N','E','X','T'+0x80,
	'L','E','T'+0x80,
	'I','F'+0x80,
	'G','O','T','O'+0x80,
	'G','O','S','U','B'+0x80,
	'R','E','T','U','R','N'+0x80,
	'R','E','M'+0x80,
	'F','O','R'+0x80,
	'I','N','P','U','T'+0x80,
	'P','R','I','N','T'+0x80,
	'P','O','K','E'+0x80,
	'S','T','O','P'+0x80,
	'B','Y','E'+0x80,
        'F','I','L','E','S'+0x80,
        'M','E','M'+0x80,
        '?'+ 0x80,
	0
};

#define KW_LIST		0
#define KW_LOAD		1
#define KW_NEW		2
#define KW_RUN		3
#define KW_SAVE		4
#define KW_NEXT		5
#define KW_LET		6
#define KW_IF		7
#define KW_GOTO		8
#define KW_GOSUB	9
#define KW_RETURN	10
#define KW_REM		11
#define KW_FOR		12
#define KW_INPUT	13
#define KW_PRINT	14
#define KW_POKE		15
#define KW_STOP		16
#define KW_BYE		17
#define KW_FILES        18
#define KW_MEM          19
#define KW_QMARK        20
#define KW_DEFAULT	21

struct stack_for_frame {
	char frame_type;
	char for_var;
	short int terminal;
	short int step;
	unsigned char *current_line;
	unsigned char *txtpos;
};

struct stack_gosub_frame {
	char frame_type;
	unsigned char *current_line;
	unsigned char *txtpos;
};

static unsigned char func_tab[] = {
	'P','E','E','K'+0x80,
	'A','B','S'+0x80,
	0
};
#define FUNC_PEEK    0
#define FUNC_ABS	 1
#define FUNC_UNKNOWN 2

static unsigned char to_tab[] = {
	'T','O'+0x80,
	0
};

static unsigned char step_tab[] = {
	'S','T','E','P'+0x80,
	0
};

static unsigned char relop_tab[] = {
	'>','='+0x80,
	'<','>'+0x80,
	'>'+0x80,
	'='+0x80,
	'<','='+0x80,
	'<'+0x80,
	0
};

#define RELOP_GE		0
#define RELOP_NE		1
#define RELOP_GT		2
#define RELOP_EQ		3
#define RELOP_LE		4
#define RELOP_LT		5
#define RELOP_UNKNOWN	6

#define STACK_SIZE (sizeof(struct stack_for_frame)*5)
#define VAR_SIZE sizeof(short int) // Size of variables in bytes

static unsigned char *stack_limit;
static unsigned char *program_start;
static unsigned char *program_end;
static unsigned char *stack; // Software stack for things that should go on the CPU stack
static unsigned char *variables_begin;
static unsigned char *current_line;
static unsigned char *sp;
#define STACK_GOSUB_FLAG 'G'
#define STACK_FOR_FLAG 'F'
static unsigned char table_index;
static LINENUM linenum;

static const unsigned char okmsg[]            = "OK";
static const unsigned char whatmsg[]          = "What? ";
static const unsigned char howmsg[]           =	"How?";
static const unsigned char sorrymsg[]         = "Sorry!";
static const unsigned char initmsg[]          = "TinyBasic in C V0.02a";
static const unsigned char memorymsg[]        = " bytes free.";
static const unsigned char breakmsg[]         = "break!";
static const unsigned char unimplimentedmsg[] = "Unimplemented";
static const unsigned char backspacemsg[]     = "\b \b";
static const unsigned char indentmsg[]        = "    ";
static const unsigned char sderrormsg[]       = "SD card error.";
static const unsigned char sdfilemsg[]        = "SD file error.";
static const unsigned char dirextmsg[]        = "(dir)";

static int inchar(void);
static void outchar(unsigned char c);
static void line_terminator(void);
static short int expression(void);
static unsigned char breakcheck(void);
/***************************************************************************/
static void ignore_blanks(void)
{
	while(*txtpos == SPACE || *txtpos == TAB)
		txtpos++;
}


/***************************************************************************/
static void scantable(unsigned char *table)
{
	int i = 0;
	table_index = 0;
	while(1)
	{
		// Run out of table entries?
		if(table[0] == 0)
            return;

		// Do we match this character?
		if(txtpos[i] == table[0])
		{
			i++;
			table++;
		}
		else
		{
			// do we match the last character of keywork (with 0x80 added)? If so, return
			if(txtpos[i]+0x80 == table[0])
			{
				txtpos += i+1;  // Advance the pointer to following the keyword
				ignore_blanks();
				return;
			}

			// Forward to the end of this keyword
			while((table[0] & 0x80) == 0)
				table++;

			// Now move on to the first character of the next word, and reset the position index
			table++;
			table_index++;
			ignore_blanks();
			i = 0;
		}
	}
}

/***************************************************************************/
static void pushb(unsigned char b)
{
	sp--;
	*sp = b;
}

/***************************************************************************/
static unsigned char popb()
{
	unsigned char b;
	b = *sp;
	sp++;
	return b;
}

/***************************************************************************/
static void printnum(int num)
{
	int digits = 0;

	if(num < 0)
	{
		num = -num;
		outchar('-');
	}

	do {
		pushb(num%10+'0');
		num = num/10;
		digits++;
	}
	while (num > 0);

	while(digits > 0)
	{
		outchar(popb());
		digits--;
	}
}
/***************************************************************************/
static unsigned short testnum(void)
{
	unsigned short num = 0;
	ignore_blanks();
	
	while(*txtpos>= '0' && *txtpos <= '9' )
	{
		// Trap overflows
		if(num >= 0xFFFF/10)
		{
			num = 0xFFFF;
			break;
		}

		num = num *10 + *txtpos - '0';
		txtpos++;
	}
	return	num;
}

/***************************************************************************/
static void printmsgNoNL(const unsigned char *msg)
{
	while(*msg)
	{
		outchar(*msg);
		msg++;
	}
}

/***************************************************************************/
static unsigned char print_quoted_string(void)
{
	int i=0;
	unsigned char delim = *txtpos;
	if(delim != '"' && delim != '\'')
		return 0;
	txtpos++;

	// Check we have a closing delimiter
	while(txtpos[i] != delim)
	{
		if(txtpos[i] == NL)
			return 0;
		i++;
	}

	// Print the characters
	while(*txtpos != delim)
	{
		outchar(*txtpos);
		txtpos++;
	}
	txtpos++; // Skip over the last delimiter

	return 1;
}

/***************************************************************************/
static void printmsg(const unsigned char *msg)
{
	printmsgNoNL(msg);
    line_terminator();
}

/***************************************************************************/
static void getln(char prompt)
{
	outchar(prompt);
	txtpos = program_end+sizeof(LINENUM);

	while(1)
	{
		char c = inchar();
		switch(c)
		{
			case NL:
				break;
			case CR:
                                line_terminator();
				// Terminate all strings with a NL
				txtpos[0] = NL;
				return;
			case CTRLH:
				if(txtpos == program_end)
					break;
				txtpos--;
				printmsg(backspacemsg);
				break;
			default:
				// We need to leave at least one space to allow us to shuffle the line into order
				if(txtpos == variables_begin-2)
					outchar(BELL);
				else
				{
					txtpos[0] = c;
					txtpos++;
#if ECHO_CHARS
					outchar(c);
#endif
				}
		}
	}
}

/***************************************************************************/
static unsigned char *findline(void)
{
	unsigned char *line = program_start;
	while(1)
	{
		if(line == program_end)
			return line;

		if(((LINENUM *)line)[0] >= linenum)
			return line;

		// Add the line length onto the current address, to get to the next line;
		line += line[sizeof(LINENUM)];
	}
}

/***************************************************************************/
static void toUppercaseBuffer(void)
{
	unsigned char *c = program_end+sizeof(LINENUM);
	unsigned char quote = 0;

	while(*c != NL)
	{
		// Are we in a quoted string?
		if(*c == quote)
			quote = 0;
		else if(*c == '"' || *c == '\'')
			quote = *c;
		else if(quote == 0 && *c >= 'a' && *c <= 'z')
			*c = *c + 'A' - 'a';
		c++;
	}
}

/***************************************************************************/
void printline()
{
	LINENUM line_num;
	
	line_num = *((LINENUM *)(list_line));
    list_line += sizeof(LINENUM) + sizeof(char);

	// Output the line */
	printnum(line_num);
	outchar(' ');
	while(*list_line != NL)
    {
		outchar(*list_line);
		list_line++;
	}
	list_line++;
	line_terminator();
}

/***************************************************************************/
static short int expr4(void)
{
	if(*txtpos == '0')
	{
		txtpos++;
		return 0;
	}

	if(*txtpos >= '1' && *txtpos <= '9')
	{
		short int a = 0;
		do 	{
			a = a*10 + *txtpos - '0';
			txtpos++;
		} while(*txtpos >= '0' && *txtpos <= '9');
		return a;
	}

	// Is it a function or variable reference?
	if(txtpos[0] >= 'A' && txtpos[0] <= 'Z')
	{
		short int a;
		// Is it a variable reference (single alpha)
		if(txtpos[1] < 'A' || txtpos[1] > 'Z')
		{
			a = ((short int *)variables_begin)[*txtpos - 'A'];
			txtpos++;
			return a;
		}

		// Is it a function with a single parameter
		scantable(func_tab);
		if(table_index == FUNC_UNKNOWN)
			goto expr4_error;

		unsigned char f = table_index;

		if(*txtpos != '(')
			goto expr4_error;

		txtpos++;
		a = expression();
		if(*txtpos != ')')
				goto expr4_error;
		txtpos++;
		switch(f)
		{
			case FUNC_PEEK:
				return program[a];
			case FUNC_ABS:
				if(a < 0) 
					return -a;
				return a;
		}
	}

	if(*txtpos == '(')
	{
		short int a;
		txtpos++;
		a = expression();
		if(*txtpos != ')')
			goto expr4_error;

		txtpos++;
		return a;
	}

expr4_error:
	expression_error = 1;
	return 0;

}

/***************************************************************************/
static short int expr3(void)
{
	short int a,b;

	a = expr4();
	while(1)
	{
		if(*txtpos == '*')
		{
			txtpos++;
			b = expr4();
			a *= b;
		}
		else if(*txtpos == '/')
		{
			txtpos++;
			b = expr4();
			if(b != 0)
				a /= b;
			else
				expression_error = 1;
		}
		else
			return a;
	}
}

/***************************************************************************/
static short int expr2(void)
{
	short int a,b;

	if(*txtpos == '-' || *txtpos == '+')
		a = 0;
	else
		a = expr3();

	while(1)
	{
		if(*txtpos == '-')
		{
			txtpos++;
			b = expr3();
			a -= b;
		}
		else if(*txtpos == '+')
		{
			txtpos++;
			b = expr3();
			a += b;
		}
		else
			return a;
	}
}
/***************************************************************************/
static short int expression(void)
{
	short int a,b;

	a = expr2();
	// Check if we have an error
	if(expression_error)	return a;

	scantable(relop_tab);
	if(table_index == RELOP_UNKNOWN)
		return a;
	
	switch(table_index)
	{
	case RELOP_GE:
		b = expr2();
		if(a >= b) return 1;
		break;
	case RELOP_NE:
		b = expr2();
		if(a != b) return 1;
		break;
	case RELOP_GT:
		b = expr2();
		if(a > b) return 1;
		break;
	case RELOP_EQ:
		b = expr2();
		if(a == b) return 1;
		break;
	case RELOP_LE:
		b = expr2();
		if(a <= b) return 1;
		break;
	case RELOP_LT:
		b = expr2();
		if(a < b) return 1;
		break;
	}
	return 0;
}

/***************************************************************************/
void loop()
{
	unsigned char *start;
	unsigned char *newEnd;
	unsigned char linelen;
	
	program_start = program;
	program_end = program_start;
	sp = program+sizeof(program);  // Needed for printnum
	stack_limit = program+sizeof(program)-STACK_SIZE;
	variables_begin = stack_limit - 27*VAR_SIZE;
	printmsg(initmsg);
	printnum(variables_begin-program_end);
	printmsg(memorymsg);

warmstart:
	// this signifies that it is running in 'direct' mode.
	current_line = 0;
	sp = program+sizeof(program);  
	printmsg(okmsg);

prompt:
	getln('>');
	toUppercaseBuffer();

	txtpos = program_end+sizeof(unsigned short);

	// Find the end of the freshly entered line
	while(*txtpos != NL)
		txtpos++;

	// Move it to the end of program_memory
	{
		unsigned char *dest;
		dest = variables_begin-1;
		while(1)
		{
			*dest = *txtpos;
			if(txtpos == program_end+sizeof(unsigned short))
				break;
			dest--;
			txtpos--;
		}
		txtpos = dest;
	}

	// Now see if we have a line number
	linenum = testnum();
	ignore_blanks();
	if(linenum == 0)
		goto direct;

	if(linenum == 0xFFFF)
		goto qhow;

	// Find the length of what is left, including the (yet-to-be-populated) line header
	linelen = 0;
	while(txtpos[linelen] != NL)
		linelen++;
	linelen++; // Include the NL in the line length
	linelen += sizeof(unsigned short)+sizeof(char); // Add space for the line number and line length

	// Now we have the number, add the line header.
	txtpos -= 3;
	*((unsigned short *)txtpos) = linenum;
	txtpos[sizeof(LINENUM)] = linelen;


	// Merge it into the rest of the program
	start = findline();

	// If a line with that number exists, then remove it
	if(start != program_end && *((LINENUM *)start) == linenum)
	{
		unsigned char *dest, *from;
		unsigned tomove;

		from = start + start[sizeof(LINENUM)];
		dest = start;

		tomove = program_end - from;
		while( tomove > 0)
		{
			*dest = *from;
			from++;
			dest++;
			tomove--;
		}	
		program_end = dest;
	}

	if(txtpos[sizeof(LINENUM)+sizeof(char)] == NL) // If the line has no txt, it was just a delete
		goto prompt;



	// Make room for the new line, either all in one hit or lots of little shuffles
	while(linelen > 0)
	{	
		unsigned int tomove;
		unsigned char *from,*dest;
		unsigned int space_to_make;
	
		space_to_make = txtpos - program_end;

		if(space_to_make > linelen)
			space_to_make = linelen;
		newEnd = program_end+space_to_make;
		tomove = program_end - start;


		// Source and destination - as these areas may overlap we need to move bottom up
		from = program_end;
		dest = newEnd;
		while(tomove > 0)
		{
			from--;
			dest--;
			*dest = *from;
			tomove--;
		}

		// Copy over the bytes into the new space
		for(tomove = 0; tomove < space_to_make; tomove++)
		{
			*start = *txtpos;
			txtpos++;
			start++;
			linelen--;
		}
		program_end = newEnd;
	}
	goto prompt;

unimplemented:
	printmsg(unimplimentedmsg);
	goto prompt;

qhow:	
	printmsg(howmsg);
	goto prompt;

qwhat:	
	printmsgNoNL(whatmsg);
	if(current_line != NULL)
	{
           unsigned char tmp = *txtpos;
		   if(*txtpos != NL)
				*txtpos = '^';
           list_line = current_line;
           printline();
           *txtpos = tmp;
	}
    line_terminator();
	goto prompt;

qsorry:	
	printmsg(sorrymsg);
	goto warmstart;

run_next_statement:
	while(*txtpos == ':')
		txtpos++;
	ignore_blanks();
	if(*txtpos == NL)
		goto execnextline;
	goto interperateAtTxtpos;

direct: 
	txtpos = program_end+sizeof(LINENUM);
	if(*txtpos == NL)
		goto prompt;

interperateAtTxtpos:
        if(breakcheck())
        {
          printmsg(breakmsg);
          goto warmstart;
        }

	scantable(keywords);

	switch(table_index)
	{
                case KW_FILES:
                        goto files;
       		case KW_LIST:
			goto list;
		case KW_LOAD:
			goto load;
                case KW_MEM:
                        goto mem;
              	case KW_NEW:
			if(txtpos[0] != NL)
				goto qwhat;
			program_end = program_start;
			goto prompt;
		case KW_RUN:
			current_line = program_start;
			goto execline;
		case KW_SAVE:
			goto save;
		case KW_NEXT:
			goto next;
		case KW_LET:
			goto assignment;
		case KW_IF:
			short int val;
			expression_error = 0;
			val = expression();
			if(expression_error || *txtpos == NL)
				goto qhow;
			if(val != 0)
				goto interperateAtTxtpos;
			goto execnextline;

		case KW_GOTO:
			expression_error = 0;
			linenum = expression();
			if(expression_error || *txtpos != NL)
				goto qhow;
			current_line = findline();
			goto execline;

		case KW_GOSUB:
			goto gosub;
		case KW_RETURN:
			goto gosub_return; 
		case KW_REM:	
			goto execnextline;	// Ignore line completely
		case KW_FOR:
			goto forloop; 
		case KW_INPUT:
			goto input; 
		case KW_PRINT:
                case KW_QMARK:
			goto print;
		case KW_POKE:
			goto poke;
		case KW_STOP:
			// This is the easy way to end - set the current line to the end of program attempt to run it
			if(txtpos[0] != NL)
				goto qwhat;
			current_line = program_end;
			goto execline;
		case KW_BYE:
			// Leave the basic interperater
			return;
		case KW_DEFAULT:
			goto assignment;
		default:
			break;
	}
	
execnextline:
	if(current_line == NULL)		// Processing direct commands?
		goto prompt;
	current_line +=	 current_line[sizeof(LINENUM)];

execline:
  	if(current_line == program_end) // Out of lines to run
		goto warmstart;
	txtpos = current_line+sizeof(LINENUM)+sizeof(char);
	goto interperateAtTxtpos;

input:
	{
		unsigned char var;
		ignore_blanks();
		if(*txtpos < 'A' || *txtpos > 'Z')
			goto qwhat;
		var = *txtpos;
		txtpos++;
		ignore_blanks();
		if(*txtpos != NL && *txtpos != ':')
			goto qwhat;
		((short int *)variables_begin)[var-'A'] = 99;

		goto run_next_statement;
	}
forloop:
	{
		unsigned char var;
		short int initial, step, terminal;
		ignore_blanks();
		if(*txtpos < 'A' || *txtpos > 'Z')
			goto qwhat;
		var = *txtpos;
		txtpos++;
		ignore_blanks();
		if(*txtpos != '=')
			goto qwhat;
		txtpos++;
		ignore_blanks();

		expression_error = 0;
		initial = expression();
		if(expression_error)
			goto qwhat;
	
		scantable(to_tab);
		if(table_index != 0)
			goto qwhat;
	
		terminal = expression();
		if(expression_error)
			goto qwhat;
	
		scantable(step_tab);
		if(table_index == 0)
		{
			step = expression();
			if(expression_error)
				goto qwhat;
		}
		else
			step = 1;
		ignore_blanks();
		if(*txtpos != NL && *txtpos != ':')
			goto qwhat;


		if(!expression_error && *txtpos == NL)
		{
			struct stack_for_frame *f;
			if(sp + sizeof(struct stack_for_frame) < stack_limit)
				goto qsorry;

			sp -= sizeof(struct stack_for_frame);
			f = (struct stack_for_frame *)sp;
			((short int *)variables_begin)[var-'A'] = initial;
			f->frame_type = STACK_FOR_FLAG;
			f->for_var = var;
			f->terminal = terminal;
			f->step     = step;
			f->txtpos   = txtpos;
			f->current_line = current_line;
			goto run_next_statement;
		}
	}
	goto qhow;

gosub:
	expression_error = 0;
	linenum = expression();
	if(!expression_error && *txtpos == NL)
	{
		struct stack_gosub_frame *f;
		if(sp + sizeof(struct stack_gosub_frame) < stack_limit)
			goto qsorry;

		sp -= sizeof(struct stack_gosub_frame);
		f = (struct stack_gosub_frame *)sp;
		f->frame_type = STACK_GOSUB_FLAG;
		f->txtpos = txtpos;
		f->current_line = current_line;
		current_line = findline();
		goto execline;
	}
	goto qhow;

next:
	// Fnd the variable name
	ignore_blanks();
	if(*txtpos < 'A' || *txtpos > 'Z')
		goto qhow;
	txtpos++;
	ignore_blanks();
	if(*txtpos != ':' && *txtpos != NL)
		goto qwhat;
	
gosub_return:
	// Now walk up the stack frames and find the frame we want, if present
	tempsp = sp;
	while(tempsp < program+sizeof(program)-1)
	{
		switch(tempsp[0])
		{
			case STACK_GOSUB_FLAG:
				if(table_index == KW_RETURN)
				{
					struct stack_gosub_frame *f = (struct stack_gosub_frame *)tempsp;
					current_line	= f->current_line;
					txtpos			= f->txtpos;
					sp += sizeof(struct stack_gosub_frame);
					goto run_next_statement;
				}
				// This is not the loop you are looking for... so Walk back up the stack
				tempsp += sizeof(struct stack_gosub_frame);
				break;
			case STACK_FOR_FLAG:
				// Flag, Var, Final, Step
				if(table_index == KW_NEXT)
				{
					struct stack_for_frame *f = (struct stack_for_frame *)tempsp;
					// Is the the variable we are looking for?
					if(txtpos[-1] == f->for_var)
					{
						short int *varaddr = ((short int *)variables_begin) + txtpos[-1] - 'A'; 
						*varaddr = *varaddr + f->step;
						// Use a different test depending on the sign of the step increment
						if((f->step > 0 && *varaddr <= f->terminal) || (f->step < 0 && *varaddr >= f->terminal))
						{
							// We have to loop so don't pop the stack
							txtpos = f->txtpos;
							current_line = f->current_line;
							goto run_next_statement;
						}
						// We've run to the end of the loop. drop out of the loop, popping the stack
						sp = tempsp + sizeof(struct stack_for_frame);
						goto run_next_statement;
					}
				}
				// This is not the loop you are looking for... so Walk back up the stack
				tempsp += sizeof(struct stack_for_frame);
				break;
			default:
				//printf("Stack is stuffed!\n");
				goto warmstart;
		}
	}
	// Didn't find the variable we've been looking for
	goto qhow;

assignment:
	{
		short int value;
		short int *var;

		if(*txtpos < 'A' || *txtpos > 'Z')
			goto qhow;
		var = (short int *)variables_begin + *txtpos - 'A';
		txtpos++;

		ignore_blanks();

		if (*txtpos != '=')
			goto qwhat;
		txtpos++;
		ignore_blanks();
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto qwhat;
		// Check that we are at the end of the statement
		if(*txtpos != NL && *txtpos != ':')
			goto qwhat;
		*var = value;
	}
	goto run_next_statement;
poke:
	{
		short int value;
		unsigned char *address;

		// Work out where to put it
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto qwhat;
		address = (unsigned char *)value;

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto qwhat;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto qwhat;
		//printf("Poke %p value %i\n",address, (unsigned char)value);
		// Check that we are at the end of the statement
		if(*txtpos != NL && *txtpos != ':')
			goto qwhat;
	}
	goto run_next_statement;

list:
	linenum = testnum(); // Retuns 0 if no line found.

	// Should be EOL
	if(txtpos[0] != NL)
		goto qwhat;

	// Find the line
	list_line = findline();
	while(list_line != program_end)
          printline();
	goto warmstart;

print:
	// If we have an empty list then just put out a NL
	if(*txtpos == ':' )
	{
        line_terminator();
		txtpos++;
		goto run_next_statement;
	}
	if(*txtpos == NL)
	{
		goto execnextline;
	}

	while(1)
	{
		ignore_blanks();
		if(print_quoted_string())
		{
			;
		}
		else if(*txtpos == '"' || *txtpos == '\'')
			goto qwhat;
		else
		{
			short int e;
			expression_error = 0;
			e = expression();
			if(expression_error)
				goto qwhat;
			printnum(e);
		}

		// At this point we have three options, a comma or a new line
		if(*txtpos == ',')
			txtpos++;	// Skip the comma and move onto the next
		else if(txtpos[0] == ';' && (txtpos[1] == NL || txtpos[1] == ':'))
		{
			txtpos++; // This has to be the end of the print - no newline
			break;
		}
		else if(*txtpos == NL || *txtpos == ':')
		{
			line_terminator();	// The end of the print statement
			break;
		}
		else
			goto qwhat;	
	}
	goto run_next_statement;

mem:
	printnum(variables_begin-program_end);
	printmsg(memorymsg);
        goto run_next_statement;
        
/*************************************************/

files:
        // display a listing of files on the device.
        // version 1: no support for subdirectories
#if ARDUINO && ENABLE_FILEIO
        if( initSD() == kSD_OK ){
          File dir = SD.open( "/" );
          while( true ) {
            File entry = dir.openNextFile();
            if( !entry ) {
              entry.close();
              break;
            }
            
            // common header
            printmsgNoNL( indentmsg );
            printmsgNoNL( (const unsigned char *)entry.name() );
            if( entry.isDirectory() ) {
                printmsgNoNL( (const unsigned char *)"/" );
            }
            
            if( entry.isDirectory() ) {
              // directory ending
              for( int i=strlen( entry.name()) ; i<16 ; i++ ) {
                printmsgNoNL( (const unsigned char *)" " );
              }
              printmsgNoNL( dirextmsg );
            } else {
              // file ending
              for( int i=strlen( entry.name()) ; i<17 ; i++ ) {
                printmsgNoNL( (const unsigned char *)" " );
              }
              printnum( entry.size() );
            }
            line_terminator();
            entry.close();
          }
          dir.close();
        }
        goto warmstart;
#else
        goto unimplemented;
#endif

load:
        // load from a file into memory
#if ARDUINO && ENABLE_FILEIO
        {
          unsigned char *filename;
          
          // Work out the filename
          expression_error = 0;
          filename = filenameWord();
          if(expression_error)
            goto qwhat;

          if( !SD.exists( (char *)filename ))
          {
            printmsg( sdfilemsg );
          } else {

            fp = SD.open( (const char *)filename );
            inFromFile = true;
            // this will kickstart a series of events to read in from the file.
          }
          
        }
        goto warmstart;
#else
        goto unimplemented;
#endif
        
save:
        // save from memory out to a file
#if ARDUINO && ENABLE_FILEIO
        {
          unsigned char *filename;

          // Work out the filename
          expression_error = 0;
          filename = filenameWord();
          if(expression_error)
            goto qwhat;
          
          // open the file, switch over to file output
          fp = SD.open( (const char *)filename, FILE_WRITE );
          outToFile = true;

	  // copied from "List"
	  list_line = findline();
  	  while(list_line != program_end)
            printline();

          // go back to standard output, close the file
          outToFile = false;
          fp.close();

          goto warmstart;
       }
#else
        goto unimplemented;
#endif

}

// returns 1 if the character is valid in a filename
static int isValidFnChar( char c )
{
  if( c >= '0' && c <= '9' ) return 1; // number
  if( c >= 'A' && c <= 'Z' ) return 1; // LETTER
  if( c >= 'a' && c <= 'z' ) return 1; // letter (for completeness)
  if( c == '_' ) return 1;
  if( c == '+' ) return 1;
  if( c == '.' ) return 1;
  if( c == '~' ) return 1;  // Window~1.txt
  
  return 0;
}

static unsigned char * filenameWord(void)
{
  // SDL - I wasn't sure if this functionality existed above, so I figured i'd put it here
  unsigned char * ret = txtpos;
  expression_error = 0;
  
  // make sure there are no quotes or spaces, search for valid characters
  //while(*txtpos == SPACE || *txtpos == TAB || *txtpos == SQUOTE || *txtpos == DQUOTE ) txtpos++;
  while( !isValidFnChar( *txtpos )) txtpos++;
  ret = txtpos;

  if( *ret == '\0' ) {
    expression_error = 1;
    return ret;
  }
  
  // now, find the next nonfnchar
  txtpos++;
  while( isValidFnChar( *txtpos )) txtpos++;
  if( txtpos != ret ) *txtpos = '\0';

  // set the error code if we've got no string
  if( *ret == '\0' ) {
    expression_error = 1;
  }
  
  return ret;
}

/***************************************************************************/
static void line_terminator(void)
{
  	outchar(NL);
	outchar(CR);
}

/***********************************************************/
void setup()
{
#if ARDUINO
  	Serial.begin(9600);	// opens serial port, sets data rate to 9600 bps
#if ENABLE_FILEIO
        initSD();
#endif
#endif
}

/***********************************************************/
static unsigned char breakcheck(void)
{
#if ARDUINO
  if(Serial.available())
    return Serial.read() == CTRLC;
  return 0;
#else
  if(kbhit())
    return getch() == CTRLC;
   else
     return 0;
#endif
}
/***********************************************************/
static int inchar()
{
#if ARDUINO
#if ENABLE_FILEIO
  if( inFromFile ) {
    // get content from a file until it's empty
    int v = fp.read();
    if( v == NL ) v=CR; // file translate
    if( !fp.available() ) {
     inFromFile = false;
     fp.close();
    }
    return v;    
  } else {
#endif
    while(1)
    {
      if(Serial.available())
        return Serial.read();
    }
#if ENABLE_FILEIO
  }
#endif
#else
	return getchar();
#endif
}

/***********************************************************/
static void outchar(unsigned char c)
{
#if ARDUINO
#if ENABLE_FILEIO
  if( outToFile ) {
    // output to a file
    fp.write( c );
  } else {
#endif
    Serial.write(c);
#if ENABLE_FILEIO
  }
#endif
#else
	putchar(c);
#endif
}

/***********************************************************/
/* SD Card helpers */

#if ARDUINO && ENABLE_FILEIO

static int initSD( void )
{
  // if the card is already initialized, we just go with it.
  // there's no support (yet?) for hot-swap of SD Cards. if you need to 
  // swap, pop the card, reset the arduino.)
  
  if( sd_is_initialized == true ) return kSD_OK;
  
  // due to the way the SD Library works, pin 10 always needs to be 
  // an output, even when your shield uses another line for CS
  pinMode(10, OUTPUT); // change this to 53 on a mega
  
  if( !SD.begin( kSD_CS )) {
    // failed
    printmsg( sderrormsg );
    return kSD_Fail;
  }
  // success - quietly return 0
  sd_is_initialized = true;
  
  // and our file redirection flags
  outToFile = false;
  inFromFile = false;

  return kSD_OK;
}
#endif