
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_PRINT(fmt, ...) fprintf(stderr, "ERROR: %s: " fmt "\n", __func__, ##__VA_ARGS__)

int tack(char* src, char** dest_ptr, int* remaining_len, int len);
int copy_param_list(char** input_ptr, char** output_ptr, int* output_remaining, int max_params);
int copy_name(char **input_ptr, char **output_buf, int *output_len,
              int *has_const, int *has_static);
int copy_type(char **type_ptr, char **buffer_ptr, int *buffer_len, char **param_ptr, int recurse_level, int is_function_type);
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

int copy_param_list(char **type_ptr, char **buffer_ptr, int *buffer_len, int recurse_level) {
    char *current_pos;
    char *saved_buffers[10][2]; // Array to store buffer start/end pairs
    int param_count = 0;
    int template_index;
    int repeat_count;
    int i;

    current_pos = *type_ptr;

    // Add opening parenthesis
    if ((*buffer_len)-- > 0) {
        *(*buffer_ptr)++ = '(';
    }

    // Handle void parameter list
    if (*current_pos == 'v') {
        if (current_pos[1] == '\0' || current_pos[1] == '_') {
            current_pos++;
            goto end_params;
        }
    }

    // Process parameters
    while (*current_pos != '\0' && *current_pos != '_') {

        // Handle template parameter references (T or N followed by digit)
        if (*current_pos == 'T' || *current_pos == 'N') {
            current_pos++;

            if (*current_pos == 'T') {
                template_index = 1;
                current_pos++;
            } else {
                template_index = *current_pos++ - '0';
            }

            // Validate template index range
            if (template_index < 1 || template_index > 9) {
                ERROR_PRINT("Invalid template index: %d", template_index);
                return -1;
            }

            // Get repeat count
            repeat_count = *current_pos++ - '0';
            if (repeat_count < 1 || repeat_count > 9 || repeat_count > param_count) {
                ERROR_PRINT("Invalid repeat count: %d", repeat_count);
                return -1;
            }

            // Calculate offset into saved_buffers array
            char **saved_pair = (char **)&saved_buffers[template_index - 1];

            // Repeat the saved parameter type
            for (i = 0; i < repeat_count; i++) {
                // Copy the saved parameter text
                int saved_len = saved_pair[1] - saved_pair[0];
                tack(saved_pair[0], buffer_ptr, buffer_len, saved_len);

                if (template_index > 0) {
                    tack(", ", buffer_ptr, buffer_len, 2);
                }

                // Save current buffer position if we have room
                if (param_count <= 9) {
                    saved_buffers[param_count][0] = saved_pair[0];
                    param_count++;
                    saved_buffers[param_count - 1][1] = saved_pair[1];
                }
            }
        }
        else {
            // Save current buffer position if we have room
            if (param_count <= 9) {
                saved_buffers[param_count][0] = *buffer_ptr;
            }

            // Process regular parameter type
            if (copy_type(&current_pos, buffer_ptr, buffer_len, NULL, recurse_level + 1, 0) != 0) {
                ERROR_PRINT("Failed to copy parameter type");
                return -1;
            }

            // Save end buffer position if we have room
            if (param_count <= 9) {
                saved_buffers[param_count][1] = *buffer_ptr;
                param_count++;
            }
        }

        // Add comma separator if more parameters follow
        if (*current_pos != '\0' && *current_pos != '_') {
            tack(", ", buffer_ptr, buffer_len, 2);
        }
    }

end_params:
    // Add closing parenthesis
    if ((*buffer_len)-- > 0) {
        *(*buffer_ptr)++ = ')';
    }

    // Update type pointer
    *type_ptr = current_pos;

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
    size_t digit_value;

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
                ERROR_PRINT("Failed to parse operator type information");
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
                        ERROR_PRINT("Too many digits in length encoding (max 3 digits)");
                        return -1;
                    }
                }
            }

            // Verify the length matches
            if (strlen(input) < digit_value) {
                ERROR_PRINT("String length %zu is less than expected length %zu", strlen(input), digit_value);
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
                ERROR_PRINT("Failed to copy user-defined operator type");
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
int copy_type(char **type_ptr, char **buffer_ptr, int *buffer_len, char **param_ptr, int recurse_level, int is_function_type) {
    char *current_pos;
    char *start_pos;
    char *temp_pos;
    char *saved_buffer;
    int digit_value;
    int string_len;
    int paren_count;
    int bracket_flag;
    char ch;
    char *type_name;

    start_pos = *type_ptr;
    current_pos = start_pos;

    // Skip over prefix characters (P, R, A followed by digits/underscore)
    while (1) {
        ch = *current_pos;

        if (ch == 'P' || ch == 'R') {
            current_pos++;
            continue;
        }

        if (ch == 'A') {
            current_pos++;
            ch = *current_pos;
            // Skip digits
            while (ch >= '0' && ch <= '9') {
                current_pos++;
                ch = *current_pos;
            }
            if (ch == '_') {
                current_pos++;
                continue;
            }
            break;
        }

        if (ch == 'C' || ch == 'V') {
            // Skip over consecutive C and V characters until P or R
            temp_pos = current_pos + 1;
            while (*temp_pos == 'C' || *temp_pos == 'V') {
                temp_pos++;
            }
            if (*temp_pos == 'P' || *temp_pos == 'R') {
                current_pos = temp_pos;
                current_pos++;
                continue;
            }
        }

        break;
    }

    temp_pos = current_pos;

    // Handle function type 'F'
    if (*current_pos == 'F') {
        current_pos++;

        if (is_function_type) {
            *type_ptr = current_pos;
            return 0;
        }

        if (recurse_level > 9) {
            ERROR_PRINT("Too many nested functions");
            return -1;
        }

        saved_buffer = *buffer_ptr;

        // Copy parameter list
        if (copy_param_list(&current_pos, buffer_ptr, buffer_len, recurse_level + 1) != 0) {
            if (*current_pos != '_') {
                ERROR_PRINT("Parameter list parsing failed and no underscore terminator found");
                return -1;
            }
            current_pos++;
        }

        *buffer_ptr = saved_buffer;

        // Recursive call for return type
        if (copy_type(&current_pos, buffer_ptr, buffer_len, param_ptr, recurse_level + 1, 0) != 0) {
            ERROR_PRINT("Failed to copy function return type");
            return -1;
        }

        *type_ptr = current_pos;
        return 0;
    }

    // Handle type modifiers (const, unsigned, volatile, signed)
    while ((*current_pos >= 'A' && *current_pos <= 'Z')) {
        ch = *current_pos++;

        switch (ch) {
            case 'C':
                tack("const ", buffer_ptr, buffer_len, 6);
                break;
            case 'M':
                ERROR_PRINT("Unsupported type modifier 'M' encountered");
                return -1;
            case 'U':
                tack("unsigned ", buffer_ptr, buffer_len, 9);
                break;
            case 'V':
                tack("volatile ", buffer_ptr, buffer_len, 9);
                break;
            case 'S':
                tack("signed ", buffer_ptr, buffer_len, 7);
                break;
            default:
                ERROR_PRINT("Unknown type modifier '%c' encountered", ch);
                return -1;
        }
    }

    // Handle numeric length prefix
    if (*current_pos >= '0' && *current_pos <= '9') {
        digit_value = *current_pos++ - '0';

        // Parse up to 3 digits
        if (*current_pos >= '0' && *current_pos <= '9') {
            digit_value = digit_value * 10 + (*current_pos++ - '0');

            if (*current_pos >= '0' && *current_pos <= '9') {
                digit_value = digit_value * 10 + (*current_pos++ - '0');

                if (*current_pos >= '0' && *current_pos <= '9') {
                    ERROR_PRINT("Too many digits in numeric length prefix (max 3 digits)");
                    return -1; // Too many digits
                }
            }
        }

        string_len = strlen(current_pos);
        if (string_len < digit_value) {
            ERROR_PRINT("Remaining string length %d is less than expected length %d", string_len, digit_value);
            return -1;
        }

        tack(current_pos, buffer_ptr, buffer_len, digit_value);
        current_pos += digit_value;
    }
    else {
        // Handle built-in types
        ch = *current_pos++;

        switch (ch) {
            case 'c': type_name = "char"; break;
            case 'd': type_name = "double"; break;
            case 'e': type_name = "long double"; break;
            case 'f': type_name = "float"; break;
            case 'i': type_name = "int"; break;
            case 'l': type_name = "long"; break;
            case 'r': type_name = "long double"; break;
            case 's': type_name = "short"; break;
            case 'v': type_name = "void"; break;
            case 'x': type_name = "long long"; break;
            case 'z': type_name = "..."; break;
            default:
                ERROR_PRINT("Unknown built-in type character '%c'", ch);
                return -1;
        }

        tack(type_name, buffer_ptr, buffer_len, 0);
    }

    *type_ptr = current_pos;

    // Add space if we've moved past the start and not at 'F'
    if (current_pos != start_pos && *current_pos != 'F') {
        if ((*buffer_len)-- > 0) {
            *(*buffer_ptr)++ = ' ';
        }
    }

    // Process pointer/reference/array suffixes
    paren_count = 0;
    current_pos = temp_pos - 1;

    while (current_pos >= start_pos) {
        bracket_flag = 0;

        // Handle array dimensions
        if (*current_pos == '_') {
            while (--current_pos >= start_pos && *current_pos == 'A') {
                paren_count++;
                bracket_flag = 1;
                break;
            }
        }

        if (paren_count && !bracket_flag) {
            if ((*buffer_len)-- > 0) {
                *(*buffer_ptr)++ = '(';
            }
            paren_count--;
        }

        if (*current_pos == 'P') {
            if ((*buffer_len)-- > 0) {
                *(*buffer_ptr)++ = '*';
            }
        }
        else if (*current_pos == 'R') {
            if ((*buffer_len)-- > 0) {
                *(*buffer_ptr)++ = '&';
            }
        }
        else if (*current_pos == 'C') {
            tack("const", buffer_ptr, buffer_len, 5);
            if (current_pos > start_pos || bracket_flag) {
                if ((*buffer_len)-- > 0) {
                    *(*buffer_ptr)++ = ' ';
                }
            }
        }
        else if (*current_pos == 'V') {
            tack("volatile", buffer_ptr, buffer_len, 8);
            if (current_pos > start_pos || bracket_flag) {
                if ((*buffer_len)-- > 0) {
                    *(*buffer_ptr)++ = ' ';
                }
            }
        }
        else {
            ERROR_PRINT("Unexpected character '%c' in pointer/reference/array suffix processing", *current_pos);
            return -1;
        }

        current_pos--;
    }

    // Handle function parameters if present
    if (param_ptr) {
        if ((*buffer_len)-- > 0) {
            *(*buffer_ptr)++ = '(';
        }

        if (copy_type(param_ptr, buffer_ptr, buffer_len, NULL, recurse_level + 1, 1) != 0) {
            ERROR_PRINT("Failed to copy function parameter type");
            return -1;
        }

        if ((*buffer_len)-- > 0) {
            *(*buffer_ptr)++ = ')';
        }

        if (copy_param_list(param_ptr, buffer_ptr, buffer_len, recurse_level + 1) != 0) {
            ERROR_PRINT("Failed to copy function parameter list");
            return -1;
        }
    }

    // Handle array dimensions
    paren_count = 0;
    current_pos = start_pos;

    while (current_pos < temp_pos) {
        if (*current_pos == 'A') {
            if (paren_count) {
                if ((*buffer_len)-- > 0) {
                    *(*buffer_ptr)++ = ')';
                }
                paren_count--;
            }

            current_pos++;
            char *underscore_pos = strchr(current_pos, '_');
            if (!underscore_pos) {
                ERROR_PRINT("Missing underscore terminator in array dimension");
                return -1;
            }

            digit_value = underscore_pos - current_pos;

            if ((*buffer_len)-- > 0) {
                *(*buffer_ptr)++ = '[';
            }

            tack(current_pos, buffer_ptr, buffer_len, digit_value);

            if ((*buffer_len)-- > 0) {
                *(*buffer_ptr)++ = ']';
            }

            current_pos = underscore_pos;
        }
        else {
            paren_count++;
        }

        current_pos++;
    }

    return 0;
}

typedef enum {
    MACSBUG_DEMANGLER_SUCCESS = 0,
    MACSBUG_DEMANGLER_NAME_FAILED = 1,
    MACSBUG_DEMANGLER_UNKNOWN_ERROR = 3,
    MACSBUG_DEMANGLER_PARAM_LIST_FAILED = 2,
    MACSBUG_DEMANGLER_BUFFER_OVERFLOW = 4,
    MACSBUG_DEMANGLER_INVALID_INPUT = 5,
    MACSBUG_DEMANGLER_INVALID_ARGS = 6,
} DemanglerErrorCode;

int unmangle(char* output, char* input, int* output_length) {
    int is_const = 0;
    int is_static = 0;

    if (copy_name(&input, &output, output_length, &is_const, &is_static) != 0) {
        return MACSBUG_DEMANGLER_NAME_FAILED;
    }

    // Check if it's a function name
    if (*input == 'F') {
        input++; // Skip the 'F'

        // Call copy_param_list with flag 0
        char *saved_output = output;
        if (copy_param_list(&input, &output, output_length, 0) != 0 || *input != '\0') {
            return MACSBUG_DEMANGLER_PARAM_LIST_FAILED;
        }

        if (is_const) {
            tack(" const", &output, output_length, 6);
        }
    } else {
        // Not a function - check if parsing is complete
        if (*input != '\0' || is_const != 0 || is_static != 0) {
            return MACSBUG_DEMANGLER_INVALID_INPUT;
        }
    }

    // Null-terminate the output
    *output = '\0';

    if (*output_length < 0) {
        return MACSBUG_DEMANGLER_BUFFER_OVERFLOW;
    } else {
        return MACSBUG_DEMANGLER_SUCCESS;
    }
}

int main(int argc, char *argv[]) {
    char output[2048];
    int output_length = sizeof(output) - 1; // Reserve space for null terminator

    // Check if we have a command-line argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mangled_name>\n", argv[0]);
        return MACSBUG_DEMANGLER_INVALID_ARGS;
    }

    // Call unmangle function with the first argument
    int status = unmangle(output, argv[1], &output_length);

    if (status == MACSBUG_DEMANGLER_SUCCESS) {
        // Success - print the demangled name
        printf("%s\n", output);
    }
    return status;
}