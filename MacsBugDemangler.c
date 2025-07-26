
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tack(char* src, char** dest_ptr, int* remaining_len, int len);
int copy_param_list(char** input_ptr, char** output_ptr, int* output_remaining, int max_params);
int copy_name(char **input_ptr, char **output_buf, int *output_len,
              int *has_const, int *has_static);
int copy_type(char** input_ptr, char** output_ptr, int* output_len, int max_depth, int is_function, char** next_ptr);
int unmangle(char* output, char* input, int* output_length);
int main(int argc, char *argv[]);

// Function to copy/append string data with length limiting
int tack(char* src, char** dest_ptr, int* remaining_len, int len) {
    char* dest = *dest_ptr;

    if (len == 0) {
        len = strlen(src);
    }

    if (len <= *remaining_len) {
        // Copy the specified length
        for (int i = 0; i < len; i++) {
            *dest++ = *src++;
        }
        *dest_ptr += len;
    } else {
        // Copy what we can
        if (*remaining_len > 0) {
            for (int i = 0; i < *remaining_len; i++) {
                *dest++ = *src++;
            }
            *dest_ptr += *remaining_len;
        }
    }

    *remaining_len -= len;
    return 0;
}

int copy_param_list(char** input_ptr, char** output_ptr, int* output_remaining, int max_params)
{
    char* input = *input_ptr;
    char* param_starts[10];
    char* param_ends[10];
    int param_count = 0;

    // Check if we need to add opening parenthesis
    if (*output_remaining > 0) {
        **output_ptr = '(';
        (*output_ptr)++;
        (*output_remaining)--;
    }

    // Handle 'v' (void) parameter case
    if (*input == 'v' && (input[1] == '\0' || input[1] == '_')) {
        input++;
        goto end_processing;
    }

    // Main parameter processing loop
    while (*input != '\0' && *input != '_') {

        // Handle template parameter references (T<digit> or N<digit>)
        if (*input == 'T' || *input == 'N') {
            input++;

            int repeat_count;
            if (*input == 'T') {
                repeat_count = 1;
            } else {
                char digit = *input;
                input++;
                repeat_count = digit - '0';
            }

            // Validate repeat count (1-9)
            if (repeat_count < 1 || repeat_count > 9) {
                return -1;
            }

            // Get parameter index
            char param_digit = *input;
            input++;
            int param_index = param_digit - '0';

            // Validate parameter index and check it's within our stored parameters
            if (param_index < 1 || param_index > 9 || param_count < param_index) {
                return -1;
            }

            // Calculate array index (parameters are 1-indexed)
            char** param_start_ptr = &param_starts[param_index - 1];

            // Repeat the parameter 'repeat_count' times
            for (int i = 0; i < repeat_count; i++) {
                // Call tack() to copy the stored parameter
                tack(param_starts[param_index - 1],
                    output_ptr,
                    output_remaining,
                    param_ends[param_index - 1] - param_starts[param_index - 1]);

                // Add comma separator if not the last repetition
                if (repeat_count > 1 && i < repeat_count - 1) {
                    tack(", ", output_ptr, output_remaining, 2);
                }

                // Store parameter bounds if we have room (max 10 parameters)
                if (param_count < 10) {
                    param_starts[param_count] = param_starts[param_index - 1];
                    param_ends[param_count] = param_ends[param_index - 1];
                    param_count++;
                }
            }
        } else {
            // Handle regular parameter - store start position if we have room
            if (param_count < 10) {
                param_starts[param_count] = *output_ptr;
            }

            // Copy the type using copy_type function
            if (copy_type(input_ptr, output_ptr, output_remaining, max_params + 1, 0, 0) != 0) {
                return -1;
            }

            // Store end position and increment parameter count
            if (param_count < 10) {
                param_ends[param_count] = *output_ptr;
                param_count++;
            }

            input = *input_ptr;
        }

        // Add comma separator between parameters (but not after the last one)
        if (*input != '\0' && *input != '_') {
            tack(", ", output_ptr, output_remaining, 2);
        }
    }

end_processing:
    // Add closing parenthesis
    if (*output_remaining > 0) {
        **output_ptr = ')';
        (*output_ptr)++;
        (*output_remaining)--;
    }

    // Update input pointer
    *input_ptr = input;

    return 0;
}

int copy_name(char **input_ptr, char **output_ptr, int *output_size,
              int *has_const, int *has_static) {
    char *input = *input_ptr;
    char *orig_input = input;
    char *saved_output = NULL;
    char *operator_name = NULL;
    int is_operator_new = 0;
    int name_length;
    int digit_value;

    *has_static = 0;
    *has_const = 0;

    // Check for "__op" prefix (operator overload)
    if (input[0] == '_' && input[1] == '_') {
        input += 2;
        if (input[0] == 'o' && input[1] == 'p') {
            input += 2;
            saved_output = *output_ptr;

            // Parse type information
            if (copy_type(&input, output_ptr, output_size, 0, 0, 0) != 0) {
                return -1;
            }

            *output_ptr = saved_output;
            goto process_rest;
        }
    }

    // Skip to next "__" or end of string
    process_rest:
    while (*input && !(input[0] == '_' && input[1] == '_')) {
        input++;
    }

    name_length = input - orig_input;

    // If we found "__", parse the encoded part
    if (*input) {
        input += 2; // skip "__"

        // Check if first character is a digit (length encoding)
        if (*input >= '0' && *input <= '9') {
            input++;
            digit_value = input[-1] - '0';

            // Parse up to 3 more digits
            if (*input >= '0' && *input <= '9') {
                digit_value = digit_value * 10 + (*input - '0');
                input++;

                if (*input >= '0' && *input <= '9') {
                    digit_value = digit_value * 10 + (*input - '0');
                    input++;

                    if (*input >= '0' && *input <= '9') {
                        // Too many digits - error
                        return -1;
                    }
                }
            }

            // Verify the length matches
            if (strlen(input) < digit_value) {
                return -1;
            }

            // Check for 'C' suffix (const)
            *has_const = (input[digit_value] == 'C') ? 1 : 0;

            // Check for 'S' suffix (static)
            *has_static = (input[digit_value + *has_const] == 'S') ? 1 : 0;

            // Add "static " prefix if needed
            if (*has_static) {
                tack("static ", output_ptr, output_size, 7);
            }

            saved_output = *output_ptr;

            // Copy the name part
            // TODO: Check on this one
            tack(input, output_ptr, output_size, digit_value);

            input += digit_value + *has_const + *has_static;
        }
    }

    // Add "::" separator if we have a saved output position
    if (saved_output) {
        tack("::", output_ptr, output_size, 2);
    }

    // Check for operator overloads
    if (orig_input[0] == '_' && orig_input[1] == '_') {
        if (name_length == 4) {
            // 4-character operator codes
            int op_code = (orig_input[2] << 8) | orig_input[3];

            switch (op_code) {
                case 0x6161: operator_name = "&&"; break;  // "aa"
                case 0x6164: operator_name = "&"; break;   // "ad"
                case 0x6173: operator_name = "="; break;   // "as"
                case 0x636c: operator_name = "()"; break; // "cl"
                case 0x636d: operator_name = ""; break;   // "cm" (comma)
                case 0x636e: operator_name = "new"; break; // "cn"
                case 0x636f: operator_name = "~"; break;  // "co"
                case 0x6463: is_operator_new = 1; break; // "dc" (destructor call)
                case 0x646c: operator_name = "delete"; break; // "dl"
                case 0x6476: operator_name = "/"; break;  // "dv"
                case 0x6571: operator_name = "=="; break; // "eq"
                case 0x6765: operator_name = ">="; break; // "ge"
                case 0x6774: operator_name = ">"; break;  // "gt"
                case 0x6c65: operator_name = "<="; break; // "le"
                case 0x6c73: operator_name = "<<"; break; // "ls"
                case 0x6c74: operator_name = "<"; break;  // "lt"
                case 0x6d64: operator_name = "%"; break;  // "md"
                case 0x6d69: operator_name = "-"; break;  // "mi"
                case 0x6d6c: operator_name = "*"; break;  // "ml"
                case 0x6d6d: operator_name = "--"; break; // "mm"
                case 0x6e65: operator_name = "!="; break; // "ne"
                case 0x6e74: operator_name = "!"; break;  // "nt"
                case 0x6f6f: operator_name = "||"; break; // "oo"
                case 0x6f72: operator_name = "|"; break;  // "or"
                case 0x706c: operator_name = "+"; break;  // "pl"
                case 0x7070: operator_name = "++"; break; // "pp"
                case 0x7273: operator_name = ">>"; break; // "rs"
                case 0x7276: operator_name = "[]"; break; // "rv"
                case 0x7872: operator_name = "^"; break;  // "xr"
                default: break;
            }
        } else if (name_length == 5 && orig_input[2] == 'a') {
            // 5-character assignment operators starting with 'a'
            int op_code = (orig_input[3] << 8) | orig_input[4];

            switch (op_code) {
                case 0x6164: operator_name = "&="; break;  // "ad"
                case 0x6461: operator_name = "+="; break;  // "da" (reversed)
                case 0x6476: operator_name = "/="; break;  // "dv"
                case 0x6572: operator_name = "^="; break;  // "er"
                case 0x6c73: operator_name = "<<="; break; // "ls"
                case 0x6d69: operator_name = "-="; break;  // "mi"
                case 0x6d6c: operator_name = "*="; break;  // "ml"
                case 0x6d64: operator_name = "%="; break;  // "md"
                case 0x6f72: operator_name = "|="; break;  // "or"
                case 0x7273: operator_name = ">>="; break; // "rs"
                default: break;
            }
        } else if (orig_input[2] == 'o' && orig_input[3] == 'p') {
            // "op" prefix - user-defined operator
            operator_name = orig_input + 4;
            is_operator_new = 1;
        }
    }

    if (operator_name) {
        tack("operator", output_ptr, output_size, 8);

        if (!is_operator_new) {
            tack(operator_name, output_ptr, output_size, 0);
        } else {
            if (copy_type(&operator_name, output_ptr, output_size, 0, 0, 0) != 0) {
                return -1;
            }
        }
    } else {
        // Copy the original name
        tack(orig_input, output_ptr, output_size, name_length);
    }

    *input_ptr = input;
    return 0;
}

// Function to copy and parse type information
int copy_type(char** input_ptr, char** output_ptr, int* output_len, int max_depth, int is_function, char** next_ptr) {
    char* input = *input_ptr;
    char* start_pos = input;
    char* current_pos = input;
    char c;

    // Skip type qualifiers (P, R, A followed by digits and _)
    while (1) {
        c = *current_pos;
        if (c == 'P' || c == 'R') {
            current_pos++;
            continue;
        }
        if (c == 'A') {
            current_pos++;
            // Skip digits
            while (*current_pos >= '0' && *current_pos <= '9') {
                current_pos++;
            }
            if (*current_pos == '_') {
                current_pos++;
                continue;
            }
            break;
        }
        if (c == 'C' || c == 'V') {
            // Skip const/volatile qualifiers
            char* temp = current_pos + 1;
            while (*temp == 'C' || *temp == 'V' || *temp == 'P' || *temp == 'R') {
                temp++;
            }
            current_pos = temp;
            continue;
        }
        break;
    }

    char* type_start = current_pos;

    // Handle function types
    if (*current_pos == 'F') {
        if (next_ptr) {
            *next_ptr = current_pos;
            return 0;
        }

        if (max_depth < 9) {
            return -1;
        }

        current_pos++;
        char* saved_output = *output_ptr;
        int saved_len = *output_len;

        // Parse parameter list
        if (copy_param_list(&current_pos, output_ptr, output_len, max_depth + 1) != 0) {
            return -1;
        }

        if (*current_pos != '_') {
            return -1;
        }
        current_pos++;

        // Restore output position and parse return type
        *output_ptr = saved_output;
        *output_len = saved_len;

        if (copy_type(&current_pos, output_ptr, output_len, max_depth + 1, 0, 0) != 0) {
            return -1;
        }

        *input_ptr = current_pos;
        return 0;
    }

    // Handle various type specifiers
    while (1) {
        c = *current_pos;
        if (c >= 'A' && c <= 'Z') {
            current_pos++;
            switch (c) {
                case 'C':
                    tack("const ", output_ptr, output_len, 6);
                    break;
                case 'M':
                    tack("unsigned ", output_ptr, output_len, 9);
                    break;
                case 'S':
                    tack("signed ", output_ptr, output_len, 7);
                    break;
                case 'V':
                    tack("volatile ", output_ptr, output_len, 9);
                    break;
                default:
                    return -1;
            }
            continue;
        }
        break;
    }

    // Handle numeric length prefixes
    if (*current_pos >= '0' && *current_pos <= '9') {
        current_pos++;
        int len = *input - '0';

        // Parse up to 3 digit length
        for (int i = 0; i < 3 && *current_pos >= '0' && *current_pos <= '9'; i++) {
            len = len * 10 + (*current_pos - '0');
            current_pos++;
        }

        if (len > 3) {
            return -1;
        }

        if (strlen(current_pos) < len) {
            return -1;
        }

        tack(current_pos, output_ptr, output_len, len);
        current_pos += len;
    } else {
        // Handle built-in types
        current_pos++;
        char* type_name = NULL;

        switch (*(current_pos - 1)) {
            case 'c': type_name = "char"; break;
            case 'i': type_name = "int"; break;
            case 'l': type_name = "long"; break;
            case 'f': type_name = "float"; break;
            case 'd': type_name = "double"; break;
            case 'r': type_name = "long double"; break;
            case 'v': type_name = "void"; break;
            case 'e': type_name = "..."; break;
            case 'x': type_name = "extended"; break;
            case 'b': type_name = "extended_80"; break;
            case 'p': type_name = "comp"; break;
            default:
                return -1;
        }

        tack(type_name, output_ptr, output_len, 0);
    }

    *next_ptr = current_pos;

    // Add space if needed
    if (current_pos != start_pos && *current_pos != 'F') {
        if (*output_len > 0) {
            **output_ptr = ' ';
            (*output_ptr)++;
            (*output_len)--;
        }
    }

    // Process pointer/reference/array qualifiers in reverse
    int paren_count = 0;
    current_pos = type_start - 1;

    while (current_pos >= start_pos) {
        c = *current_pos;

        if (c == '_') {
            // Skip array dimensions
            current_pos--;
            while (current_pos >= start_pos && *current_pos != 'A') {
                current_pos--;
            }
            if (current_pos < start_pos) break;

            if (paren_count > 0) {
                if (*output_len > 0) {
                    **output_ptr = ')';
                    (*output_ptr)++;
                    (*output_len)--;
                }
                paren_count--;
            }

            // Find array start
            char* array_start = current_pos + 1;
            char* underscore_pos = strchr(array_start, '_');
            if (!underscore_pos) return -1;

            int array_len = underscore_pos - array_start;

            if (*output_len > 0) {
                **output_ptr = '[';
                (*output_ptr)++;
                (*output_len)--;
            }

            tack(array_start, output_ptr, output_len, array_len);

            if (*output_len > 0) {
                **output_ptr = ']';
                (*output_ptr)++;
                (*output_len)--;
            }

            current_pos = underscore_pos;
        } else {
            paren_count++;
        }

        current_pos--;
    }

    // Handle function parameters if this is a function
    if (is_function) {
        if (*output_len > 0) {
            **output_ptr = '(';
            (*output_ptr)++;
            (*output_len)--;
        }

        if (copy_type(next_ptr, output_ptr, output_len, max_depth + 1, 1, 0) != 0) {
            return -1;
        }

        if (*output_len > 0) {
            **output_ptr = ')';
            (*output_ptr)++;
            (*output_len)--;
        }

        if (copy_param_list(next_ptr, output_ptr, output_len, max_depth + 1) != 0) {
            return -1;
        }
    }

    *input_ptr = *next_ptr;
    return 0;
}

int unmangle(char* output, char* input, int* output_length) {
    int local_var1;  // A6 - 0x4
    int local_var2;  // A6 - 0x8

    // Call copy_name function
    if (copy_name(input, output, output_length, &local_var2, &local_var1) != 0) {
        return 0;
    }

    // Check if input starts with 'F' (function marker)
    if (*input == 'F') {
        input++; // Skip the 'F'

        // Call copy_param_list with flag 0
        if (copy_param_list(input, output, output_length, 0) != 0 || *input != '\0') {
            return -1;
        }

        // If local_var2 is set, append " const"
        if (local_var2) {
            tack(" const", output, output_length, 6);
        }
    } else {
        // Not a function - check if parsing is complete
        if (*input != '\0' || local_var2 != 0 || local_var1 != 0) {
            return -1;
        }
    }

    // Null-terminate the output
    *output = '\0';

    // Return status based on output_length
    if (*output_length < 0) {
        return 2;  // Buffer overflow
    } else {
        return 1;  // Success
    }
}

int main(int argc, char *argv[]) {
    char output[2048];
    int output_length = sizeof(output) - 1; // Reserve space for null terminator

    // Check if we have a command-line argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mangled_name>\n", argv[0]);
        return 1;
    }

    // Call unmangle function with the first argument
    int status = unmangle(output, argv[1], &output_length);

    if (status == 1) {
        // Success - print the demangled name
        printf("%s\n", output);
        return 0;
    } else {
        // Error occurred
        fprintf(stderr, "Demangling failed with status: %d\n", status);
        return 1;
    }
}