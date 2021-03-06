#include "ysh.h"

int main(int argc, char **argv)
{
	char input_string[MAX_COMMAND_LENGTH] = {0}, *parsed_args[MAX_COMMANDS];
	char *parsed_args_piped[MAX_COMMANDS];
	int ch, x_initial, y, x, input_string_position = 0, history_counter = 0;
	int num_shifts = 0, history_index;

	output_buffer = malloc(200 * sizeof(char));
	ysh_path = str_cat_realloc(NULL, argv[0]);

	init_shell();

	while (1)
	{
		print_primary_prompt_string();
		getyx(stdscr, y, x_initial);
		reset_input_string(input_string, &input_string_position);
		history_counter = 0;
		num_shifts = 0;
		do
		{
			// buffer verification for arrow keys and signals
			ch = getch();
			switch (ch)
			{
			case KEY_UP:
				history_index = history_length - ++history_counter;
				getyx(stdscr, y, x);
				move(y, x_initial);
				clrtoeol();
				if (history_index >= 0)
					update_input_string_with_history(history_index, input_string, &input_string_position);
				else
				{
					reset_input_string(input_string, &input_string_position);
					history_counter--;
				}
				break;
			case KEY_DOWN:
				history_index = history_length - --history_counter;
				getyx(stdscr, y, x);
				move(y, x_initial);
				clrtoeol();
				if (history_index < history_length)
					update_input_string_with_history(history_index, input_string, &input_string_position);
				else
				{
					reset_input_string(input_string, &input_string_position);
					history_counter++;
				}
				break;
			case KEY_LEFT:
				// shift string only when a character is entered
				getyx(stdscr, y, x);
				// force to stay after prompt_string
				if (x <= x_initial)
					move(y, x_initial);
				else
				{
					move(y, x - 1);
					input_string_position--;
					num_shifts++;
				}
				break;
			case KEY_RIGHT:
				// move cursor right and shift string right
				getyx(stdscr, y, x);
				if (num_shifts > 0)
				{
					move(y, x + 1);
					input_string_position++;
					num_shifts--;
				}
				break;
			case KEY_BACKSPACE:
				getyx(stdscr, y, x);
				// force to stay after prompt_string
				if (x <= x_initial)
				{
					move(y, x_initial);
					clrtoeol();
					reset_input_string(input_string, &input_string_position);
				}
				else
				{
					move(y, x - 1);
					input_string_position--;
					shift_input_string(input_string, input_string_position, -1 - num_shifts);
					getyx(stdscr, y, x);
					move(y, x_initial);
					printw("%s", input_string);
					move(y, x);
				}
				break;
			case CTRL_Z:
				// TODO: put the foreground process to sleep
				getyx(stdscr, y, x);
				mvprintw(y, x_initial, "  ");
				move(y, x_initial);
				break;
			case CTRL_C:
				printw("\n");
				print_primary_prompt_string();
				break;
			case CTRL_D:
				destroy_shell();
				return 0;
			case CTRL_L:
				clear();
				print_primary_prompt_string();
				break;
			case '\n':
				print_primary_prompt_string();
				printw("\n");
				break;
			// echo ba  ta
			default:
				if (isalnum(ch) || '$' || '_' || ' ' || '\t')
				{
					shift_input_string(input_string, input_string_position, 1);

					input_string[input_string_position] = ch;
					input_string_position++;
					getyx(stdscr, y, x);
					move(y, x_initial);
					printw("%s", input_string);
					move(y, x);
				}
				break;
			}
			refresh();
		} while (ch != '\n');

		//input_string[input_string_position] = '\0';

		if (verify_input(input_string))
			continue;

		add_command_to_history(input_string);

		exec_control_sequence(input_string, parsed_args, parsed_args_piped);

		if (exit_flag)
			break;

		print_output_buffer();
		refresh();
	}

	destroy_shell();

	return 0;
}

void init_shell()
{
	initscr();							// Start curses mode
	raw();									// Line buffering disabled
	keypad(stdscr, TRUE);		// enables keypad to use the arrow keys to scroll on the process list
	scrollok(stdscr, TRUE); // enables scroll
	idlok(stdscr, TRUE);
	nl();
	using_history();
	// read if there's a history in ~/.history
	read_history(NULL);
	config_environment_variables();
}

void config_environment_variables()
{
	// $MYPATH
	char *env_path = getenv("PATH");
	setenv("MYPATH", env_path, 1);
	// $MYPS1
	char *default_myps1 = "\\u@\\h:\\w\\$ ";
	if (getenv("MYPS1") == NULL)
		setenv("MYPS1", default_myps1, 1);

	// $0 - shell path
	// remove "."
	strsep(&ysh_path, ".");
	char *_pwd = getenv("PWD");
	char *pwd = str_cat_realloc(NULL, _pwd);
	// concat to get the shell absolute path
	pwd = str_cat_realloc(pwd, ysh_path);
	setenv("0", pwd, 1);
	free(ysh_path - 1);
	free(pwd);
}

void print_primary_prompt_string()
{
	// TODO: tint with some colors to make it more beautiful
	char *_prompt_string = getenv("MYPS1");
	char *start_address = NULL, *prompt_string;

	start_address = prompt_string = str_cat_realloc(NULL, _prompt_string);

	for (char *c = prompt_string; *c != '\0'; c++)
	{
		if (*c != '\\')
			printw("%c", *c);
		else
			c = parse_prompt_string_special_characters(c);
	}

	if (start_address)
		free(start_address);
}

char *parse_prompt_string_special_characters(char *string)
{
	char *buffer = malloc(BUFFER_SIZE * sizeof(char));
	char *buffer_start_address = buffer;
	char *input_string = NULL, **parsed_args = NULL, **parsed_args_piped = NULL;
	char *_input_string = NULL;
	uid_t uid;
	int ret;
	struct stat *stat_buf = NULL;

	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = localtime(&rawtime);

	switch (string[1])
	{
	// \a : an ASCII bell character (07)
	case 'a':
		printf("\a");
		break;
	// \d : the date in “Weekday Month Date” format (e.g., “Tue May 26”)
	case 'd':
		strftime(buffer, BUFFER_SIZE, "%a %b %d", timeinfo);
		printw("%s", buffer);
		break;
	// \D{format} : the format is passed to strftime(3) and the result is inserted into the prompt string;
	// an empty format results in a locale-specific time representation. The braces are required.
	case 'D':
		if (string[2] == '{')
		{
			string += 3;
			strftime(buffer, BUFFER_SIZE, strsep(&string, "}"), timeinfo);
			printw("%s", buffer);
			string--;
		}
		else
			printw("\nysh: MYPS1 syntax error: '\\D{format}'");
		string--;
		break;
	// \e : an ASCII escape character (027)
	case 'e':
		printf("\e");
		break;
	// \h : the hostname up to the first ‘.’
	case 'h':
		ret = gethostname(buffer, BUFFER_SIZE);
		if (ret == 0)
			printw("%s", strsep(&buffer, "."));
		break;
	// \H : the hostname
	case 'H':
		ret = gethostname(buffer, BUFFER_SIZE);
		if (ret == 0)
			printw("%s", buffer);
		break;
	// \j : the number of jobs currently managed by the shell
	case 'j':
		printw("%d", count_jobs());
		break;
	// \l : the basename of the shell's terminal device name
	case 'l':
		stat(getenv("0"), stat_buf);
		if (stat_buf != NULL)
			printf("%d", minor(stat_buf->st_dev));
		break;
	// \n : newline
	case 'n':
		printw("\n");
		break;
	// \r : carriage return
	case 'r':
		printw("\r");
		break;
	// \s : the name of the shell, the basename of $0 (the portion following the final slash)
	case 's':
		printw("ysh");
		break;
	// \t : the current time in 24-hour HH:MM:SS format
	case 't':
		strftime(buffer, BUFFER_SIZE, "%T", timeinfo);
		printw("%s", buffer);
		break;
	// \T : the current time in 12-hour HH:MM:SS format
	case 'T':
		strftime(buffer, BUFFER_SIZE, "%r", timeinfo);
		printw("%s", strsep(&buffer, " PM"));
		break;
	// \@ : the current time in 12-hour am/pm format
	case '@':
		strftime(buffer, BUFFER_SIZE, "%r", timeinfo);
		printw("%s:", strsep(&buffer, ":"));
		printw("%s", strsep(&buffer, ":"));
		break;
	// \A : the time, in 24-hour HH:MM format.
	case 'A':
		strftime(buffer, BUFFER_SIZE, "%R", timeinfo);
		printw("%s", buffer);
		break;
	// \u : the username of the current user.
	case 'u':
		buffer = getenv("USER");
		printw("%s", buffer);
		break;
	// \v : the version of Bash (e.g., 2.00)
	case 'v':
		buffer = str_cat_realloc(NULL, version);
		printw("%s.", strsep(&buffer, "."));
		printw("%s", strsep(&buffer, "."));
		break;
	// \V : the release of Bash, version + patchlevel (e.g., 2.00.0)
	case 'V':
		printw("%s", version);
		break;
	// \w : the current working directory, with $HOME abbreviated with a tilde.
	case 'w':
		buffer = getcwd(buffer, BUFFER_SIZE);
		buffer = abbreviate_home(buffer);
		printw("%s", buffer);
		break;
	// \W : the basename of the current working directory, with $HOME abbreviated with a tilde.
	case 'W':
		buffer = get_cwd_basename(buffer);
		printw("%s", buffer);
		break;
	// \! : the history number of this command
	case '!':
		printw("%d", history_length);
		break;
	// \# : the command number of this command
	case '#':
		printw("%d", command_number + 1);
		break;
	// \$ : if the effective UID is 0, a #, otherwise a $
	case '$':
		uid = geteuid();
		if (uid == 0)
			printw("#");
		else
			printw("$");
		break;
	// \\ : a backslash
	case '\\':
		printw("\\");
		break;
	// \[ : begin a sequence of non-printing characters, which could be used to embed a terminal control sequence into the prompt
	case '[':
		string += 2;
		_input_string = input_string = calloc(MAX_COMMAND_LENGTH, sizeof(char));
		// \] : end a sequence of non-printing characters
		input_string = strsep(&string, "\\]");
		if (verify_input(input_string) == 0)
		{
			parsed_args = calloc(MAX_COMMANDS, sizeof(char *));
			parsed_args_piped = calloc(MAX_COMMANDS, sizeof(char *));
			add_command_to_history(input_string);
			exec_control_sequence(input_string, parsed_args, parsed_args_piped);
		}
		string--;
		break;
	default:
		// \nnn : the character corresponding to the octal number
		string++;
		char c = _handle_escape_n_base(&string, OCTAL);
		// do not print special characters directly
		if (c > 31)
			printw("%c", c);
		else
			printf("%c", c);
		break;
	}

	free(buffer_start_address);

	if (_input_string)
		free(_input_string);
	if (parsed_args_piped)
		free(parsed_args_piped);
	if (parsed_args)
		free(parsed_args);

	return ++string;
}

int count_jobs()
{
	int ret = 0;

	for (int i = 0; i <= most_recent_job_index; i++)
	{
		if (jobs_list[i].pid != 0)
			ret++;
	}

	return ret;
}

char parse_escape_n_base(char **escape_sequence, int base)
{
	char ret = 0;

	if (isdigit(escape_sequence[0][0]))
	{
		char *str_dif;
		ret = (int)strtol(escape_sequence[0], &str_dif, base);
		escape_sequence[0] += (str_dif - escape_sequence[0]);
	}

	return ret;
}

char _handle_escape_n_base(char **escape_sequence, int base)
{
	char ret = 0;

	ret = parse_escape_n_base(escape_sequence, base);
	if (ret != 0)
		escape_sequence[0]--;
	else
		ret = escape_sequence[0][0];

	return ret;
}

char *abbreviate_home(char *cwd)
{
	char *home = getenv("HOME");
	int home_length = strlen(home);

	if (strncasecmp(cwd, home, home_length) == 0)
	{
		char *c = (cwd + home_length);
		snprintf(cwd, strlen(cwd), "~%s", c);
	}

	return cwd;
}

char *get_cwd_basename(char *cwd)
{
	char *temp = NULL;

	cwd = getcwd(cwd, BUFFER_SIZE);
	cwd = abbreviate_home(cwd);

	do
	{
		temp = strsep(&cwd, "/");
	} while (cwd);

	return temp;
}

void reset_input_string(char *input_string, int *input_string_position)
{
	*input_string_position = 0;
	input_string[0] = '\0';
}

void update_input_string_with_history(int history_index, char *input_string, int *input_string_position)
{
	HIST_ENTRY *history_entry = history_list()[history_index];
	int new_length = strlen(history_entry->line);

	printw("%s", history_entry->line);

	strncpy(input_string, history_entry->line, new_length);
	*input_string_position = new_length;
}

int shift_input_string(char *input_string, int start_position, int num_shifts)
{
	int ret = 0;
	char *start_address = strchr(input_string, '\0');
	char *end_address = input_string + start_position - 1;
	char *next_position;
	int loop_increment = -1;
	int swap_increment = 1;
	//printw("shift start addr: %d end addr : %d", *start_address, *end_address);
	//refresh();
	if (num_shifts < 0)
	{
		//if (num_shifts == -1)
		//{
		//	*end_address = '\0';
		//	return ret;
		//}

		*(end_address + 1) = '\0';
		start_address = input_string + start_position + 1;
		end_address = strchr(input_string, '\0') + 1;
		loop_increment = 1;
		swap_increment = -1;
	}

	for (char *c = start_address; c != end_address; c += loop_increment)
	{
		// printf("current char: %c\n", *c);
		next_position = c + swap_increment;
		if (is_valid_input_string_position(input_string, next_position))
		{
			swap_char(c, next_position);
			// printf("\tswap %c with %c\n", *(c+i*swap_multiplier), *(c+(i+1)*swap_multiplier));
		}
		else
			ret = -1;
	}

	return ret;
}

int is_valid_input_string_position(char *string, char *position_address)
{
	if ((position_address - string) > MAX_COMMAND_LENGTH || position_address < string)
		return 0;
	else
		return 1;
}

void swap_char(char *a, char *b)
{
	char aux = *a;
	*a = *b;
	*b = aux;
}

int verify_input(char *input_string)
{
	if (strlen(input_string) > 0)
		return 0;
	else
		return -1;
}

void add_command_to_history(char *input_string)
{
	// TODO: do not add if it's equal to the previous entry
	// history_entry = previous_history();
	// if (strcmp(buf, history_entry->line) != 0)
	add_history(input_string);
}

void exec_control_sequence(char *input_string, char **parsed_args, char **parsed_args_piped)
{
	command_type exec_flag = process_input_string(input_string, parsed_args, parsed_args_piped);

	switch (exec_flag)
	{
	case SIMPLE:
		exec_system_command(parsed_args);
		break;
	case PIPE:
		exec_system_command_piped(parsed_args, parsed_args_piped);
		break;
	default:
		break;
	}

	command_number++;
}

command_type process_input_string(char *input_string, char **parsed_args, char **parsed_args_piped)
{
	char *input_string_piped[MAX_PIPED_PROGRAMS];
	int piped = 0;
	char **parsed_redirects = (char **)malloc(MAX_COMMANDS);

	piped = parse_pipe(input_string, input_string_piped);

	if (piped)
	{
		parse_background(&(input_string_piped[0]));
		parse_background(&(input_string_piped[1]));

		parse_whitespaces(input_string_piped[0], parsed_args);
		parse_redirects(input_string_piped[0], parsed_redirects, parsed_args);
		//parsed_args = parsed_redirects;

		parse_whitespaces(input_string_piped[1], parsed_args_piped);
		parse_redirects(input_string_piped[1], parsed_redirects, parsed_args_piped);
		//parsed_args_piped = parsed_redirects;
	}
	else
	{
		parse_background(&input_string);
		parse_whitespaces(input_string, parsed_args);
		parse_redirects(input_string, parsed_redirects, parsed_args);
		//parsed_args = parsed_redirects;
	}
	if (parsed_args[0] != NULL && handle_builtin_commands(parsed_args) == 0)
		return BUILTIN;
	else
		return SIMPLE + piped;
}

void parse_background(char **input_string)
{
	char *string2separate = str_cat_realloc(NULL, *input_string);

	//free(*input_string);
	*input_string = strsep(&string2separate, "&");

	if (string2separate != NULL)
	{
		current_context = BACKGROUND;
	}
	else
		current_context = FOREGROUND;
}

int parse_pipe(char *input_string, char **input_string_piped)
{
	for (int i = 0; i < MAX_PIPED_PROGRAMS; i++)
	{
		input_string_piped[i] = strsep(&input_string, "|");
		if (input_string_piped[i] == NULL)
			break;
	}

	if (input_string_piped[1] == NULL)
		return 0; // returns zero if no pipe is found.
	else
		return 1;
}

void parse_whitespaces(char *input_string, char **parsed)
{
	int i = 0;
	char double_quote = '"';
	char *double_quote_position = NULL;
	char *whitespace_position = NULL;
	char *parsed_quotes = NULL;

	for (i = 0; i < MAX_COMMANDS - 1; i++)
	{
		if (input_string)
		{
			double_quote_position = strchr(input_string, double_quote);
			whitespace_position = strchr(input_string, ' ');
		}
		else
		{
			parsed[i] = NULL;
			i++;
			break;
		}

		// whitespace is after or inside double quotes
		if ((double_quote_position && whitespace_position && (double_quote_position < whitespace_position)) ||
				(double_quote_position && whitespace_position == NULL))
		{
			// store what's before first double_quote
			parsed_quotes = strsep(&input_string, &double_quote);
			if (parsed_quotes && strlen(parsed_quotes) > 0)
				parsed[i++] = parsed_quotes;
			// store what's inside double_quotes
			parsed[i] = strsep(&input_string, &double_quote);
			if (input_string == NULL)
			{
				printw("ysh: syntax error: missing closing '\"'\n");
				parsed[0] = NULL;
				return;
			}
		}
		else
			parsed[i] = strsep(&input_string, " ");

		if (parsed[i] == NULL)
		{
			++i;
			break;
		}
		if (strlen(parsed[i]) == 0)
			i--;
	}
	parsed[i] = NULL;
}

command_type handle_builtin_commands(char **parsed_args)
{
	builtin_command current_command = match_builtin_command(parsed_args[0]);
	int ret = 0;

	switch (current_command)
	{
	case BG:
		_bg(parsed_args[1]);
		break;
	case CD:
		ret = change_dir(parsed_args[1]);
		break;
	case CLEAR:
		clear();
		break;
	case ECHO:
		_echo(parsed_args + 1);
		break;
	case EXIT:
		exit_flag = 1;
		break;
	case EXPORT:
		export(parsed_args + 1);
		break;
	case FG:
		// TODO:fg
		break;
	case HELP:
		print_help();
		break;
	case HISTORY:
		print_commands_history();
		break;
	case JOBS:
		print_jobs();
		break;
	case KILL:
		_kill(parsed_args + 1);
		break;
	case SET:
		_set();
		break;
	default:
		// it must check if it's a system command
		ret = SIMPLE;
		break;
	}

	if (ret == -1)
	{
		snprintf(output_buffer, BUFFER_SIZE, "%s: %s: %s\n", parsed_args[0], strerror(errno), parsed_args[1]);
		ret = BUILTIN;
	}

	return ret;
}

builtin_command match_builtin_command(char *input)
{
	int i = 0;

	for (char **command = builtin_commands_list; *command != NULL; command++, i++)
	{
		if (strcmp(input, *command) == 0)
			return i;
	}

	return COMMAND_NOT_FOUND;
}

void _bg(char *job2recover)
{
	int job_num = most_recent_job_index;

	if (job2recover != NULL)
	{
		strsep(&job2recover, "%%");
		job_num = strtol(job2recover, NULL, DECIMAL);
	}

	if (jobs_list[job_num].pid == 0)
	{
		snprintf(output_buffer, BUFFER_SIZE, "bg: no job at %d \n", job_num);
		return;
	}

	kill(jobs_list[job_num].pid, SIGCONT);
}

int change_dir(char *path)
{
	char *cwd;
	int ret = chdir(path);
	if (ret == 0)
	{
		cwd = getcwd(NULL, 0);
		if (cwd == NULL)
			ret = -1;
		else
			setenv("PWD", cwd, 1);
	}

	return ret;
}

void _echo(char **message)
{
	char aux[2];
	// it's a env variable
	if (message[0] && message[0][0] == '$')
	{
		// +1 to skip '$'
		char *env_variable = getenv(message[0] + 1);
		if (env_variable == NULL)
			snprintf(output_buffer, BUFFER_SIZE, "echo: environment variable '%s' doesn't exist\n", message[0] + 1);
		else
			snprintf(output_buffer, BUFFER_SIZE, "%s\n", env_variable);
	}
	else if (message[0])
	{
		// it's a string list
		for (char **string = message; *string != NULL; string++)
		{
			for (char *c = *string; *c != '\0'; c++)
			{
				char _c = *c;
				if (*c == '\\')
				{
					c++;
					_c = escape_sequence_to_char(&c);
				}
				snprintf(aux, 2, "%c", _c);
				output_buffer = str_cat_realloc(output_buffer, aux);
			}
			output_buffer = str_cat_realloc(output_buffer, " ");
		}
		output_buffer = str_cat_realloc(output_buffer, "\n");
	}
	else
		output_buffer = str_cat_realloc(output_buffer, "\n");
}

char escape_sequence_to_char(char **escape_sequence)
{
	char ret;

	switch (escape_sequence[0][0])
	{
	case 'a':
		ret = '\a';
		break;
	case 'b':
		ret = '\b';
		break;
	case 'e':
		ret = '\e';
		break;
	case 't':
		ret = '\t';
		break;
	case 'n':
		ret = '\n';
		break;
	case 'v':
		ret = '\v';
		break;
	case 'f':
		ret = '\f';
		break;
	case 'r':
		ret = '\r';
		break;
	case '\\':
		ret = '\\';
		break;
	case '"':
		ret = '\"';
		break;
	case '\'':
		ret = '\'';
		break;
	case '?':
		ret = '\?';
		break;
	case 'x':
		escape_sequence[0]++;
		// \xhh
		ret = _handle_escape_n_base(escape_sequence, HEXADECIMAL);
		break;
	default:
		// \nnn
		ret = _handle_escape_n_base(escape_sequence, OCTAL);
		break;
	}

	return ret;
}

void export(char **config_args)
{
	char *env_variable, *append_env_variable, *append_env_variable_value = NULL;
	char *aux_config_start, *aux_config = NULL;
	int ret = 0;

	if (config_args[0] == NULL)
	{
		snprintf(output_buffer, BUFFER_SIZE, "Usage: export 'ENV_VAR'=[$APPEND_VAR:]'NEW_VALUE'\n");
		return;
	}

	// this must be done because strsep changes the aux_config pointer.
	aux_config_start = aux_config = str_cat_realloc(NULL, config_args[0]);

	// aux_config now contains the new value
	env_variable = strsep(&aux_config, "=");

	if (getenv(env_variable) == NULL)
		snprintf(output_buffer, BUFFER_SIZE, "export: environment variable '%s' doesn't exist\n", env_variable);
	else if (aux_config != NULL && aux_config[0] == '$')
	{
		append_env_variable = strsep(&aux_config, ":");
		// + 1 to skip '$'
		char *temp_value = getenv(append_env_variable + 1);
		if (temp_value != NULL)
		{
			append_env_variable_value = str_cat_realloc(NULL, temp_value);
			// if a ':' was found in strsep
			if (aux_config != NULL)
			{
				str_cat_realloc(append_env_variable_value, ":");
				if (strlen(aux_config) > 0)
					str_cat_realloc(append_env_variable_value, aux_config);
				// value is a string between double quotes
				else
					str_cat_realloc(append_env_variable_value, config_args[1]);
			}
			ret = setenv(env_variable, append_env_variable_value, 1);
		}
		else
			snprintf(output_buffer, BUFFER_SIZE, "export: environment variable '%s' doesn't exist\n", (append_env_variable + 1));
	}
	else if (aux_config == NULL)
		snprintf(output_buffer, BUFFER_SIZE, "export: syntax error: missing '='\n");
	else if (strlen(aux_config) > 0)
		ret = setenv(env_variable, aux_config, 1);
	// value is a string between double quotes
	else if (config_args[1])
		ret = setenv(env_variable, config_args[1], 1);
	else
		ret = setenv(env_variable, "", 1);

	if (ret == -1)
		snprintf(output_buffer, BUFFER_SIZE, "export: setenv error: %s", strerror(errno));

	if (aux_config_start)
		free(aux_config_start);

	if (append_env_variable_value)
		free(append_env_variable_value);
}

void print_help()
{
	// TODO: implement help [command]
	char print_string[] = "\tYSH General Commands Manual\n\n"
												"builtin: bg, cd, clear, echo, exit, export, fg, help, history, jobs, kill, set\n"
												"run: \n"
												"	builtin [-options] [args ...]\n"
												"\nFor more info about each command run help [command]\n";

	snprintf(output_buffer, BUFFER_SIZE, "%s", print_string);
}

void print_commands_history()
{
	register HIST_ENTRY **history;
	char buffer[MAX_COMMAND_LENGTH + SMALL_STRING_SIZE];
	history = history_list();
	int i = history_length > MAX_COMMANDS_HISTORY ? history_length - (MAX_COMMANDS_HISTORY + 1) : 0;

	if (history != NULL)
	{
		for (; history[i]; i++)
		{
			if (history[i]->line != NULL)
			{
				snprintf(buffer, MAX_COMMAND_LENGTH + SMALL_STRING_SIZE, "%d %s\n", i, history[i]->line);
				output_buffer = str_cat_realloc(output_buffer, buffer);
			}
		}
	}
}

void print_jobs()
{
	char buffer[MAX_COMMAND_LENGTH + 50];
	char *state = malloc(SMALL_STRING_SIZE * sizeof(char));
	job _job;

	for (int i = 0; i <= most_recent_job_index; i++)
	{
		_job = jobs_list[i];
		if (_job.pid != 0)
		{
			job_state_to_string(_job.state, &state);
			snprintf(buffer, MAX_COMMAND_LENGTH, "[%d] + %s", i + 1, state);
			output_buffer = str_cat_realloc(output_buffer, buffer);
			for (char *arg = *_job.command; arg != NULL; arg++)
				output_buffer = str_cat_realloc(output_buffer, arg);
		}
	}

	free(state);
}

void job_state_to_string(job_state state, char **string)
{
	switch (state)
	{
	case RUNNING:
		snprintf(*string, SMALL_STRING_SIZE, "running");
		break;
	case STOPPED:
		snprintf(*string, SMALL_STRING_SIZE, "stopped");
		break;
	case TERMINATED:
		snprintf(*string, SMALL_STRING_SIZE, "terminated");
		break;
	case DONE:
		snprintf(*string, SMALL_STRING_SIZE, "done");
		break;
	}
}

void _kill(char **parsed_args)
{
	char signal_list[] = "1 HUP 2 INT 3 QUIT 4 ILL 5 TRAP 6 ABRT 7 BUS\n"
											 "8 FPE 9 KILL 10 USR1 11 SEGV 12 USR2 13 PIPE 14 ALRM\n"
											 "15 TERM 16 STKFLT 17 CHLD 18 CONT 19 STOP 20 TSTP 21 TTIN \n"
											 "22 TTOU 23 URG 24 XCPU 25 XFSZ 26 VTALRM 27 PROF 28 WINCH\n"
											 "29 POLL 30 PWR 31 SYS\n";
	int signal = SIGTERM;
	int pid_index = 0, error = 0, job_index = 0;
	char *pid_string;
	pid_t pid = 0;

	if (parsed_args[0] == NULL || strlen(parsed_args[0]) == 0)
		error = 1;
	else if (parsed_args[0][0] == '-')
	{
		if (isdigit(parsed_args[0][1]))
		{
			signal = strtol(parsed_args[0] + 1, NULL, DECIMAL);
			pid_index = 1;
		}
		else if (parsed_args[0][1] == 'l')
		{
			output_buffer = str_cat_realloc(output_buffer, signal_list);
			return;
		}
		else if (strcmp(parsed_args[0], "-s") == 0 || strcmp(parsed_args[0], "--signal") == 0)
		{
			signal = strtol(parsed_args[1], NULL, DECIMAL);
			pid_index = 2;
		}
	}
	else
		error = 1;

	if (parsed_args[pid_index] && !error)
	{
		pid_string = strsep(&parsed_args[pid_index], "%%");
		if (parsed_args[pid_index] == NULL)
			pid = strtol(pid_string, NULL, DECIMAL);
		else
		{
			job_index = strtol(parsed_args[pid_index], NULL, DECIMAL);
			if (job_index <= most_recent_job_index && jobs_list[job_index].pid != 0)
			{
				pid = jobs_list[job_index].pid;
				jobs_list[job_index].pid = 0;
			}
			else
			{
				snprintf(output_buffer, BUFFER_SIZE, "kill: %%%d: no such job", job_index);
				return;
			}
		}
		kill(pid, signal);
	}
	else
		snprintf(output_buffer, BUFFER_SIZE, "kill: not enough arguments\n");
}

void _set()
{
	for (char **variable = environ; *variable != NULL; variable++)
	{
		output_buffer = str_cat_realloc(output_buffer, *variable);
		output_buffer = str_cat_realloc(output_buffer, "\n");
	}
}

void parse_redirects(char *input_string, char **parsed_redirects, char **parsed_args)
{
	int argc = 0, new_arg_flag = 0, first_redirect = MAX_REDIRECT_ARGS - 1;
	char *string2separate, *separator_ret = NULL, *redirect_sign = NULL;
	char separator;

	for (char **arg = parsed_args; *arg != NULL; arg++)
	{
		string2separate = str_cat_realloc(NULL, *arg);

		new_arg_flag = 0;
		for (char *c = string2separate; *c != '\0'; c++)
		{
			switch (*c)
			{
			case '<':
				separator = *c;
				separator_ret = strsep(&string2separate, "<");
				break;
			case '>':
				separator = *c;
				separator_ret = strsep(&string2separate, ">");
				break;
			case '2':
				if (*(c + 1) == '>')
				{
					separator = *c;
					separator_ret = strsep(&string2separate, "2>");
					string2separate++;
				}
				else
					separator_ret = NULL;
				break;
			default:

				separator_ret = NULL;
				break;
			}
			if (separator_ret != NULL)
			{
				if (strlen(separator_ret) > 0)
				{
					argc = update_arg_count(&argc, &new_arg_flag);
					parsed_redirects[argc] = separator_ret;
					if (argc < first_redirect)
						parsed_args[argc] = separator_ret;
				}
				redirect_sign = (char *)malloc(2);
				redirect_sign[0] = separator;
				redirect_sign[1] = '\0';
				argc = update_arg_count(&argc, &new_arg_flag);
				first_redirect = argc < first_redirect ? argc : first_redirect;
				parsed_redirects[argc] = redirect_sign;
				c = string2separate;
			}
		}
		if (strlen(string2separate) > 0)
		{
			argc = update_arg_count(&argc, &new_arg_flag);
			parsed_redirects[argc] = str_cat_realloc(NULL, string2separate);
		}
		argc++;
	}

	parsed_redirects[argc] = NULL;
	parsed_args[first_redirect] = NULL;
	handle_redirect(parsed_redirects);
}

int update_arg_count(int *argc, int *new_arg_flag)
{
	if (*new_arg_flag)
		(*argc)++;
	else
		*new_arg_flag = 1;
	return *argc;
}

void handle_redirect(char **parsed_redirects)
{
	char **arg = parsed_redirects;
	char *redirect_file, *current_dir = str_cat_realloc(NULL, getenv("PWD"));
	int argc = 0, arg_end = 0, redirect_flag = 0;

	current_dir = str_cat_realloc(current_dir, "/");
	redirection_file_stream.error_stream = NULL;

	redirection_file_stream.input_stream = NULL;
	redirection_file_stream.input_stream = NULL;
	redirection_file_stream.output_stream = NULL;
	for (; *arg != NULL; arg++)
	{
		redirect_file = str_cat_realloc(NULL, current_dir);
		switch (**arg)
		{
		case '<':
			redirect_file = str_cat_realloc(redirect_file, *(arg + 1));
			redirection_file_stream.input_stream = redirect_file;
			arg_end += redirect_flag * argc;
			redirect_flag = 0;
			break;
		case '>':
			redirect_file = str_cat_realloc(redirect_file, *(arg + 1));
			redirection_file_stream.output_stream = redirect_file;
			arg_end += redirect_flag * argc;
			redirect_flag = 0;
			break;
		case '2':
			redirect_file = str_cat_realloc(redirect_file, *(arg + 1));
			redirection_file_stream.error_stream = redirect_file;
			arg_end += redirect_flag * argc;
			redirect_flag = 0;
			break;
		default:
			break;
		}
		argc++;
	}
	if (arg_end != 0)
		parsed_redirects[arg_end] = NULL;
	//clean_redirects(parsed_redirects);
	return;
}

void clean_redirects(char **parsed_redirects)
{
	for (int i = 1; parsed_redirects[i] != NULL; i++)
		free(parsed_redirects[i]);
	free(parsed_redirects[0]);
}

void exec_system_command(char **parsed_args)
{
	// Forking a child
	pid_t pid = fork();

	if (pid == -1)
	{
		snprintf(output_buffer, BUFFER_SIZE, "ysh: Failed to fork: %s\n", strerror(errno));
		return;
	}
	else if (pid == 0)
	{
		if (exec_mypath(parsed_args[0], parsed_args) < 0)
			handle_exec_error(parsed_args[0]);
	}
	else
	{
		// waiting for child to terminate
		if (current_context == FOREGROUND)
			wait(NULL);
		else
			job_list_append(pid, parsed_args);
		raw();
		fflush(stdout);
	}

	return;
}

void job_list_append(pid_t pid, char **parsed_args)
{
	jobs_list[++most_recent_job_index].pid = pid;
	copy_args(jobs_list[most_recent_job_index].command, parsed_args);
	jobs_list[most_recent_job_index].state = RUNNING;
}

void copy_args(char **list_dest, char **list_src)
{
	char *list_src_iterator = *list_src;
	for (; list_src_iterator != NULL; list_src_iterator++)
	{
		*list_dest = str_cat_realloc(NULL, list_src_iterator);
		list_dest++;
	}
	*list_dest = NULL;
}

int exec_mypath(const char *file, char *const argv[])
{
	char *mypath = getenv("MYPATH");
	int ret = 0;
	if (mypath)
	{
		char mypathenv[strlen(mypath) + sizeof("MYPATH=")];
		sprintf(mypathenv, "MYPATH=%s", mypath);
		char *envp[] = {mypathenv, NULL};
		update_IO();
		//noraw();
		ret = execvpe(file, argv, envp);
	}
	else
	{
		snprintf(output_buffer, BUFFER_SIZE, "ysh: MYPATH env variable doesn't exist.\n");
		ret = -1;
	}

	return ret;
}

void update_IO()
{
	FILE *input_file = NULL, *output_file = NULL, *err_file = NULL;

	if (redirection_file_stream.input_stream != NULL)
	{
		handle_file_open(&input_file, "r", redirection_file_stream.input_stream);
		dup2(fileno(input_file), STDIN);
	}
	else if (current_context == BACKGROUND)
	{
		input_file = fdopen(STDIN, "r");
		fclose(input_file);
	}

	if (redirection_file_stream.output_stream != NULL)
	{
		handle_file_open(&output_file, "w+", redirection_file_stream.output_stream);
		dup2(fileno(output_file), STDOUT);
	}

	if (redirection_file_stream.error_stream != NULL)
	{
		handle_file_open(&err_file, "w+", redirection_file_stream.error_stream);
		dup2(fileno(err_file), STDERR);
	}
}

void handle_exec_error(char *command)
{
	if (errno = ENOENT)
		snprintf(output_buffer, BUFFER_SIZE, "ysh: command not found: %s\n", command);
	else
		snprintf(output_buffer, BUFFER_SIZE, "ysh: Failed to exec command: %s: ", strerror(errno));
	refresh();
	exit(EXIT_FAILURE);
}

void exec_system_command_piped(char **parsed_args, char **parsed_args_piped)
{
	int pipe_fd[2];
	pid_t p1, p2;

	if (pipe(pipe_fd) < 0)
	{
		snprintf(output_buffer, BUFFER_SIZE, "ysh: Pipe could not be initialized: %s", strerror(errno));
		return;
	}
	p1 = fork();
	if (p1 < 0)
	{
		snprintf(output_buffer, BUFFER_SIZE, "ysh: Failed to fork: %s\n", strerror(errno));
		return;
	}

	if (p1 == 0)
	{
		// It only needs to write at the write end
		close(pipe_fd[0]);
		dup2(pipe_fd[1], STDOUT_FILENO);
		close(pipe_fd[1]);

		if (exec_mypath(parsed_args[0], parsed_args) < 0)
			handle_exec_error(parsed_args[0]);
	}
	else
	{
		// Parent executing
		if (current_context == BACKGROUND)
			job_list_append(p1, parsed_args);

		p2 = fork();

		if (p2 < 0)
		{
			snprintf(output_buffer, BUFFER_SIZE, "ysh: Failed to fork: %s\n", strerror(errno));
			return;
		}

		// It only needs to read at the read end
		if (p2 == 0)
		{
			close(pipe_fd[1]);
			dup2(pipe_fd[0], STDIN_FILENO);
			close(pipe_fd[0]);
			if (exec_mypath(parsed_args_piped[0], parsed_args_piped) < 0)
				handle_exec_error(parsed_args_piped[0]);
		}
		else
		{
			// parent executing, waiting for two children
			if (current_context == BACKGROUND)
				job_list_append(p2, parsed_args_piped);

			wait(NULL);
			wait(NULL);
			raw();
		}
	}
}

void print_output_buffer()
{
	FILE *output_file = NULL;

	if (redirection_file_stream.output_stream != NULL &&
			handle_file_open(&output_file, "w+", redirection_file_stream.output_stream) == -1)
		printw("echo: failed to redirect output to file '%s': %s", redirection_file_stream.output_stream, strerror(errno));

	if (output_file == NULL)
		printw("%s", output_buffer);
	else
		fprintf(output_file, "%s", output_buffer);

	if (output_file != NULL)
		fclose(output_file);

	output_buffer[0] = 0;
}

void destroy_shell()
{
	// dumps the current history to the file ~/.history
	write_history(NULL);
	endwin(); /* End curses mode		  */
	free(output_buffer);
}

int handle_file_open(FILE **file_stream, const char *mode, const char *file_name)
{
	if (file_stream != NULL)
	{
		*file_stream = fopen(file_name, mode);
		if (*file_stream == NULL)
		{
			// snprintf(error_buffer, BUFFER_SIZE, "Could not open file \"%s\"", file_name);
			// perror(error_buffer);
			return -1;
		}
	}
	return 0;
}

char *str_cat_realloc(char *destiny, const char *source)
{
	size_t destiny_length = destiny ? strlen(destiny) : 0, source_length = strlen(source);
	size_t new_length = destiny_length + source_length + 1 /* NULL */;
	char *ret = destiny ? realloc(destiny, new_length) : malloc(new_length);

	if (ret)
	{
		memcpy(ret + destiny_length, source, source_length + 1 /* NULL */);
		ret[destiny_length + source_length] = 0;
	}

	return ret;
}
