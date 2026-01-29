#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>

int words_max = 100;
int max_strlen = 1000;
char cwd[PATH_MAX];
_Bool semicolon = false, is_background = false, parentheses = false, has_special_symbol = false;


_Bool check_parentheses_balance(const char *line) {
    int balance = 0;
    while (*line != '\0') {
        if (*line == '(') {
            balance++;
        } else if (*line == ')') {
            balance--;
            if (balance < 0) {
                return false; // Закрывающая скобка без соответствующей открывающей
            }
        }
        line++;
    }
    return balance == 0; // Все открывающие скобки имеют соответствующие закрывающие
}

// Функция для добавления слова в массив аргументов
void add_word(char **words, int *word_count, const char *start, int length)
{
    if (length <= 0)
        return;
    words[*word_count] = (char *)calloc(length + 1, sizeof(char));
    strncpy(words[*word_count], start, length);
    words[*word_count][length] = '\0';
    (*word_count)++;
    if (*word_count >= words_max - 1)
    {
        words_max *= 2;
        words = (char **)realloc(words, sizeof(*words) * words_max);
        if (words == NULL)
        {
            perror("Ошибка выделения памяти");
            exit(1);
        }
    }
}

// Функция для выполнения встроенной команды `cd`
int my_cd(char **args)
{
    if (args[1] == NULL)
    {
        // Если аргумент не указан, переходим в домашний каталог
        const char *home = getenv("HOME");
        if (home == NULL)
        {
            fprintf(stderr, "Не удалось получить домашний каталог.\n");
            return 1;
        }
        if (chdir(home) != 0)
        {
            perror("cd");
        }
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            strcpy(cwd, "Введите команду:  ");
        }
    }
    else
    {
        // Переход в указанную директорию
        if (chdir(args[1]) != 0)
        {
            perror("cd");
        }
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            strcpy(cwd, "Введите команду:  ");
        }
    }
    return 1;
}

// Разбирает строку на отдельные слова (аргументы) и возвращает их массив
char **process_line(char *line, int *word_count)
{
    if (!check_parentheses_balance(line)) {
        fprintf(stderr, "Ошибка: несбалансированные скобки\n");
        return NULL;
    }

    char **words = (char **)calloc(words_max, sizeof(*words));
    if (words == NULL)
    {
        perror("Ошибка выделения памяти");
        exit(1);
    }

    *word_count = 0;
    char *buffer = (char *)calloc(max_strlen, sizeof(char)); // временный буфер для текущего слова
    int buffer_index = 0;
    char *ptr = line;
    int in_quotes = 0; // Флаг для отслеживания двойных кавычек

    while (*ptr != '\0')
    {
        if (*ptr == '"')
        {
            in_quotes = !in_quotes; // Переключаем режим кавычек
        }
        else if (isspace(*ptr) && !in_quotes)
        {
            // Завершаем слово, если находимся вне кавычек и встречаем пробел
            if (buffer_index > 0)
            {
                add_word(words, word_count, buffer, buffer_index);
                buffer_index = 0; // Очищаем буфер
            }
        }
        else if (strchr("&|;><()", *ptr) && !in_quotes)
        {
			if (*ptr == ';') {
				semicolon = true;
			}
            if (*ptr == '(') {
				parentheses = true;
			}
            // Если управляющий символ, добавляем текущее слово, затем символ как отдельное слово
            if (buffer_index > 0)
            {
                add_word(words, word_count, buffer, buffer_index);
                buffer_index = 0; // Очищаем буфер
            }

            // Проверка на двойные символы (&&, ||, >>)
            if ((*ptr == '&' && *(ptr + 1) == '&') ||
                (*ptr == '|' && *(ptr + 1) == '|') ||
                (*ptr == '>' && *(ptr + 1) == '>'))
            {
                char buff2[3] = {0};
                buff2[0] = *ptr;
                buff2[1] = *ptr;
                add_word(words, word_count, buff2, 2);
                ptr += 1;
            }
            else
            {
                buffer[0] = *ptr;
                add_word(words, word_count, buffer, 1);
            }
        }
        else
        {
            // Добавляем символ в буфер
            buffer[buffer_index++] = *ptr;
            if (buffer_index >= max_strlen - 1)
            {
                max_strlen *= 2;
                buffer = (char *)realloc(buffer, sizeof(*buffer) * max_strlen);
            }
        }
        ptr++;
    }

    // Добавляем последнее слово, если оно осталось в буфере
    if (buffer_index > 0)
    {
        add_word(words, word_count, buffer, buffer_index);
    }
    free(buffer);
    // Вывод слов построчно
    /*
    for (int i = 0; i < *word_count; i++) {
        if (!in_quotes) {
            printf("%s\n", words[i]);
        }
        // free(words[i]);
        // Освобождаем память после вывода (для следующих этапов убрать)
    }
    */

    // Проверка на незакрытые кавычки
    if (in_quotes)
    {
        fprintf(stderr, "Ошибка ввода: неправильное число кавычек\n");
        for (int i = 0; i < *word_count; i++)
        {
            free(words[i]);
        }
        free(words);
        *word_count = 0;
        return NULL;
    }
    return words;
}

// Функция для обработки перенаправления ввода-вывода
int handle_redirection(char **args, int *input_fd, int *output_fd)
{
    for (int i = 0; args[i] != NULL; i++)
    {
        // Перенаправление ввода
        if (strcmp(args[i], "<") == 0)
        {
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Ошибка: файл для ввода не указан\n");
                return -1;
            }
            *input_fd = open(args[i + 1], O_RDONLY);
            if (*input_fd < 0)
            {
                perror("Ошибка открытия файла для чтения");
                return -1;
            }
            free(args[i]);
            free(args[i + 1]);
            args[i] = NULL;
            args[i + 1] = NULL;
            // Перенаправление вывода
        }
        else if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0)
        {
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Ошибка: файл для вывода не указан\n");
                return -1;
            }
            int flags = O_WRONLY | O_CREAT | (strcmp(args[i], ">>") == 0 ? O_APPEND : O_TRUNC);
            *output_fd = open(args[i + 1], flags, 0644);
            if (*output_fd < 0)
            {
                perror("Ошибка открытия файла для записи");
                return -1;
            }
            free(args[i]);
            free(args[i + 1]);
            args[i] = NULL;
            args[i + 1] = NULL;
        }
    }
    return 0;
}

// Выполняет команды, связанные пайпами (конвейер)
int execute_pipeline(char **args) {
    int pipe_fd[2], input_fd = 0, i = 0, output_fd = STDOUT_FILENO;
    char *command[words_max];
    for (int i = 0; i > words_max; i++) {
        command[i] = NULL;
    }
    pid_t pid;

    while (args[i] != NULL) {
        int j = 0;
        // Извлекаем команду до символа `|`
        while (args[i] != NULL && strcmp(args[i], "|") != 0) {
            command[j++] = args[i++];
        }
        command[j] = NULL;
        if (args[i] != NULL) i++; // Пропускаем `|`

        pipe(pipe_fd);

        pid = fork();
        if (pid == 0) {
            // Дочерний процесс
            dup2(input_fd, STDIN_FILENO); // Вход - результат предыдущей команды
            if (args[i] != NULL) dup2(pipe_fd[1], STDOUT_FILENO);

            if (handle_redirection(command, &input_fd, &output_fd) < 0) {
                return -1;
            } else {
                if (input_fd != STDIN_FILENO)
                {
                    dup2(input_fd, STDIN_FILENO);
                    close(input_fd);
                }
                if (output_fd != STDOUT_FILENO)
                {
                    dup2(output_fd, STDOUT_FILENO);
                    dup2(output_fd, STDERR_FILENO); // Перенаправляем stderr в output_fd (для вывода ошибок в файл)
                    close(output_fd);
                }
            }

            close(pipe_fd[0]);
            execvp(command[0], command);
            fprintf(stderr, "Ошибка выполнения команды: %s\n", command[0]);
            exit(EXIT_FAILURE); // Команда завершилась ошибкой                  
        } else if (pid < 0) {
            perror("Ошибка создания процесса");
            continue; // Пропускаем текущую команду
        }
        close(pipe_fd[1]);
        input_fd = pipe_fd[0]; // Вход следующего процесса - выход текущего
    }

    close(input_fd); // Закрываем последний вход
    while (wait(NULL) > 0); // Ждём завершения всех процессов
    return 0;
}

// Удаление зомби-процессов
void remove_zombie_processes()
{
    int status;
    pid_t pid;

    // Цикл, который вызывает waitpid, пока есть завершенные дочерние процессы
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFEXITED(status))
        {
            printf("Процесс %d завершился с кодом %d\n", pid, WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status))
        {
            printf("Процесс %d завершился из-за сигнала %d\n", pid, WTERMSIG(status));
        }
    }
}

// Функция для ожидания завершения процесса и получения его статуса
int wait_for_process(pid_t pid) {
    int status;
    pid_t result = waitpid(pid, &status, 0);
    if (result == -1) {
        perror("waitpid");
        return -1;
    }

    if (WIFEXITED(status)) {
        //printf("Процесс %d завершился с кодом %d\n", pid, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        //printf("Процесс %d завершился из-за сигнала %d\n", pid, WTERMSIG(status));
        return -1;
    } else {
        //printf("Процесс %d завершился по неизвестной причине\n", pid);
        return -1;
    }
}

// Функция для выполнения одиночной команды
int execute_single_command(char **args, int first_index, int last_index) {
    char **commands = args;
    int index = 0;
    if (first_index != -1 && last_index != -1) {
        commands = (char **) calloc(last_index - first_index + 2, sizeof(char *));
        for (int i = first_index; i <= last_index; i++) {
            if (strcmp(args[i], "#") == 0) {
                continue;
            }
            commands[index++] = args[i];
        }
    } else if (has_special_symbol) {
        commands = (char **) calloc(words_max, sizeof(char *));
        for (int i = first_index; i <= last_index; i++) {
            if (strcmp(args[i], "#") == 0) {
                continue;
            }
            commands[index++] = args[i];
        }
    }

	int input_fd = STDIN_FILENO, output_fd = STDOUT_FILENO, status = 0;

    // Нужно добавить проверку на наличие "&&" и "||" и их обработку
    _Bool double_pipe = false, double_ampersand = false, semicolon = false;

    for (int i = 0; commands[i] != NULL; i++) {
        if (strcmp(commands[i], "&&") == 0) {
            double_ampersand = true;
            break;
        } else if (strcmp(commands[i], "||") == 0) {
            double_pipe = true;
            break;
        } else if (strcmp(commands[i], ";") == 0) {
            semicolon = true;
            break;
        }
    }
	
    if (semicolon == true) {
		int first_index = 0, last_index = 0, i = 0; // Индекс начала текущей команды
		// Проверяем, если есть оператор `;` (команды в `args` разделены `;`)
		for (i = 0; args[i] != NULL; i++) {
			if (strcmp(args[i], ";") == 0) {
				// Выполняем все команды, разделенные `;`
				args[i] = NULL;  // Оставляем только команду до `;`

				// Выполняем текущую команду
				execute_single_command(args, first_index, i - 1);
				first_index = i + 1;

				// Пропускаем пустые элементы массива, связанные с разделителем `;`
				continue;
			}
		}
		execute_single_command(args, first_index, i - 1);
	}

    if (double_pipe || double_ampersand) {
        int first_index = 0, last_index = 0, i = 0; // Индекс начала текущей команды
		_Bool execute = true;
        // Проверяем, если есть оператор `&&` или `||` (команды в `args` разделены `&&` или `||`)
        for (i = 0; commands[i] != NULL; i++) {
            if (strcmp(commands[i], "&&") == 0 || strcmp(commands[i], "||") == 0) {
                // Выполняем текущую команду
                status = execute_single_command(commands, first_index, i - 1);
                if ((strcmp(commands[i], "||") == 0 && status == 0) || (strcmp(commands[i], "&&") == 0 && status != 0)) {
                    // Если команда завершилась с ощибкой и есть `&&`, или команда завершилась успешно и есть `||`
                    // Пропускаем оставшиеся команды
					execute = false;
                    break;
                }
                first_index = i + 1;

                // Пропускаем пустые массива, связанные с разделителем `&&` или `||`
                continue;
            }
        }
		if (execute) {
			return execute_single_command(commands, first_index, i - 1);
		}
        return status;
    }
	
    // Если есть конвейеры, выполняем их
    for (int i = 0; commands[i] != NULL; i++) {
        if (strcmp(commands[i], "|") == 0) {
            status = execute_pipeline(commands);
            if ((first_index != -1 && last_index != -1) || has_special_symbol)
                free(commands);
            return status;
        }
    }

    // Обработка перенаправления ввода-вывода
    if (handle_redirection(commands, &input_fd, &output_fd) < 0) {
        if ((first_index != -1 && last_index != -1) || has_special_symbol)
            free(commands);
        return -1;
    }

    // Проверка на команду cd
    if (strcmp(commands[0], "cd") == 0) {
        status = my_cd(commands); // Выполняем cd в текущем процессе
        if ((first_index != -1 && last_index != -1) || has_special_symbol)
            free(commands);
        return status;
    }

    // Обычное выполнение команды
    pid_t pid = fork();
    if (pid == 0)
    {
        // Дочерний процесс
        if (is_background) {
            // Перенаправляем ввод и вывод на /dev/null
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd < 0) {
                perror("Ошибка открытия /dev/null");
                exit(EXIT_FAILURE);
            }
            dup2(null_fd, STDIN_FILENO);
            //dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);

            // Игнорируем сигнал SIGINT
            signal(SIGINT, SIG_IGN);
            printf("\n");
        }

        if (input_fd != STDIN_FILENO)
        {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO)
        {
            dup2(output_fd, STDOUT_FILENO);
            dup2(output_fd, STDERR_FILENO); // Перенаправляем stderr в output_fd (для вывода ошибок в файл)
            close(output_fd);
        }
        execvp(commands[0], commands);
        perror("Ошибка выполнения команды");
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        perror("Ошибка создания процесса");
    }
    else
    {
        if (!is_background)
        {
            status = wait_for_process(pid); // Ждём завершения команды
            if ((first_index != -1 && last_index != -1) || has_special_symbol)
                free(commands);
            return status;
        }
        else
        {
            printf("Процесс %d запущен в фоновом режиме\n", pid);
        }
    }

    if ((first_index != -1 && last_index != -1) || has_special_symbol)
        free(commands);
    return 0;
}

// Выполняет команды с учетом фонового режима
int execute_command(char **args)
{
    remove_zombie_processes();
	is_background = false;
	int status = 0;

    // Обработка флага фонового режима
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "&") == 0) {
            is_background = true;
            free(args[i]);
            args[i] = NULL;
            if (args[i + 1] != NULL) {
                return -1;
            }
            break;
        }
    }
	
    if (parentheses) {
        // Обработка скобок
        while (parentheses) {
            int start = -1, end = -1;
            for (int i = 0; args[i] != NULL; i++) {
                if (strcmp(args[i], "(") == 0) {
                    start = i;
                } else if (strcmp(args[i], ")") == 0) {
                    end = i;
                    break;
                }
            }
            
            if (start != -1 && end != -1) {
                // Выполняем команду в скобках
                status = execute_single_command(args, start + 1, end - 1);

                // Перезаписываем команду
                free(args[start]);
                if (!is_background) {
                    if (status == 0) {
                        args[start] = strdup("true");
                    } else {
                        args[start] = strdup("false");
                    }
                }
                for (int i = (is_background ? start : start + 1); i <= end; i++) {
                    free(args[i]);
                    args[i] = strdup("#"); // Специальный символ
                    has_special_symbol = true;
                }
            } else {
                parentheses = false;
            }
        }
    } else {
        // Выполнение команды вне скобок
        status = execute_single_command(args, -1, -1);
    }
	
	return status;
}

// Функция получения строки
int get_line(char *line)
{
    int c, i = 0;
    while ((c = getchar()) != EOF && c != '\n')
    {
        line[i++] = c;
        if (i >= max_strlen - 1)
        {
            max_strlen *= 2;
            line = (char *)realloc(line, sizeof(*line) * max_strlen);
        }
    }
    line[i] = '\0';
    if (c == EOF)
    {
        return EOF;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, SIG_IGN); // Игнорирование сигналов

    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        cwd[0] = '\0';
    }

    FILE *fr = NULL;
    if (argc > 1)
    {
        fr = freopen(argv[1], "r", stdin);
        if (fr == NULL)
        {
            perror("File is not found.\n");
            exit(1);
        }
    }

    char *line = (char *)calloc(max_strlen, sizeof(char));
    int args_len = 0;
    char **args = NULL;

    while ((fr == NULL ? printf("%s$ Введите команду: ", cwd) : 0, get_line(line) != EOF))
    {
        semicolon = false;
        is_background = false;
        parentheses = false;
        has_special_symbol = false;
        args = process_line(line, &args_len);
        if (args_len != 0)
        {
            // Выполняем команду, если она не пустая
            execute_command(args);
        }
        for (int i = 0; i < args_len; i++)
        {
            free(args[i]);
        }
        free(args);
    }

    if (line[0] != '\0')
    {
        args = process_line(line, &args_len);
        if (args_len != 0)
        {
            // Выполняем команду, если она не пустая
            execute_command(args);
        }
        for (int i = 0; i < args_len; i++)
        {
            // Освобождаем память аргументов команды
            free(args[i]);
        }
        free(args); // Освбождаем память под массив аргументов
    }
    free(line);

    // Удаляем завершенные фоновые процессы
    //remove_zombie_processes();

    if (fr != NULL)
    {
        fclose(fr);
    }
    return 0;
}

