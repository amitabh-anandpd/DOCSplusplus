#include "../../include/common.h"
#include "../../include/write.h"

#define MAX_SENTENCES 200
#define MAX_SENT_LEN 4096
#define MAX_WORDS 512

int is_delim(char c) {
    return (c == '.' || c == '!' || c == '?');
}

// --- Helper: split text into sentences ---
int split_sentences(const char *text, char sentences[MAX_SENTENCES][MAX_SENT_LEN]) {
    int count = 0, idx = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        sentences[count][idx++] = text[i];
        if (is_delim(text[i])) {
            sentences[count][idx] = '\0';
            count++;
            idx = 0;
        }
    }
    if (idx > 0) {
        sentences[count][idx] = '\0';
        count++;
    }
    return count;
}

// --- Helper: split sentence into words ---
int split_words(const char *sentence, char words[MAX_WORDS][128]) {
    int count = 0;
    char tmp[MAX_SENT_LEN];
    strcpy(tmp, sentence);
    char *token = strtok(tmp, " ");
    while (token && count < MAX_WORDS) {
        strcpy(words[count++], token);
        token = strtok(NULL, " ");
    }
    return count;
}

int is_locked(const char *filename, int sentence_num) {
    char lock_path[512];
    sprintf(lock_path, "%s/%s.%d.lock", STORAGE_DIR, filename, sentence_num);
    return access(lock_path, F_OK) == 0;
}

void create_lock(const char *filename, int sentence_num) {
    char lock_path[512];
    sprintf(lock_path, "%s/%s.%d.lock", STORAGE_DIR, filename, sentence_num);
    FILE *fp = fopen(lock_path, "w");
    if (fp) fclose(fp);
}

void remove_lock(const char *filename, int sentence_num) {
    char lock_path[512];
    sprintf(lock_path, "%s/%s.%d.lock", STORAGE_DIR, filename, sentence_num);
    remove(lock_path);
}

// --- Core WRITE logic ---
void write_to_file(int client_sock, const char *filename, int sentence_num) {
    char path[512];
    sprintf(path, "%s/%s", STORAGE_DIR, filename);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        // Create empty file if it doesn't exist
        fp = fopen(path, "w");
        if (!fp) {
            char msg[128];
            sprintf(msg, "ERROR: Could not create file '%s'.\n", filename);
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        fclose(fp);
        fp = fopen(path, "r");
    }

    // Read entire file content
    char file_buf[16384];
    size_t bytes = fread(file_buf, 1, sizeof(file_buf) - 1, fp);
    file_buf[bytes] = '\0';
    fclose(fp);

    // Split into sentences
    char sentences[MAX_SENTENCES][MAX_SENT_LEN];
    int sentence_count = split_sentences(file_buf, sentences);

    // For empty files, only sentence 0 is valid
    // For non-empty files:
    // - If last char is delimiter, allow writing up to sentence_count
    // - If no trailing delimiter, only allow writing up to sentence_count-1
    int max_sentence;
    if (bytes == 0) {
        // Empty file - only allow sentence 0
        if (sentence_num != 0) {
            char msg[256];
            sprintf(msg, "ERROR: File is empty. Only sentence 0 can be edited.\n");
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        sentence_count = 0;  // Start fresh
    } else {
        // Check if file ends with a delimiter
        char last_char = file_buf[bytes - 1];
        max_sentence = is_delim(last_char) ? sentence_count : sentence_count - 1;
        
        if (sentence_num < 0 || sentence_num > max_sentence) {
            char msg[256];
            if (max_sentence < 0) max_sentence = 0;
            sprintf(msg, "ERROR: Invalid sentence number. Valid range is 0 to %d%s\n",
                    max_sentence,
                    is_delim(last_char) ? " (file ends with punctuation)." : ".");
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
    }

    if (is_locked(filename, sentence_num)) {
        char msg[128];
        sprintf(msg, "ERROR: Sentence %d is locked by another user.\n", sentence_num);
        send(client_sock, msg, strlen(msg), 0);
        return;
    }

    create_lock(filename, sentence_num);

    // Initialize empty working sentence for new sentences
    char working_sentence[MAX_SENT_LEN] = "";
    if (sentence_num < sentence_count) {
        strcpy(working_sentence, sentences[sentence_num]);
    }

    char msg[128];
    sprintf(msg, "Sentence %d locked. You may begin writing.\n", sentence_num);
    send(client_sock, msg, strlen(msg), 0);

    while (1) {
        char recv_buf[1024] = {0};
        read(client_sock, recv_buf, sizeof(recv_buf));

        // ETIRW → finish
        if (strncmp(recv_buf, "ETIRW", 5) == 0) {
            strcpy(sentences[sentence_num], working_sentence);
            
            // If this was a new sentence at the end, increment count
            if (sentence_num >= sentence_count) {
                sentence_count = sentence_num + 1;
            }
            
            // Ensure non-empty content
            if (strlen(working_sentence) == 0) working_sentence[0] = '.';

            // Recombine file
            fp = fopen(path, "w");
            if (!fp) {
                char err[] = "ERROR: Unable to save file.\n";
                send(client_sock, err, strlen(err), 0);
                remove_lock(filename, sentence_num);
                return;
            }
            for (int i = 0; i < sentence_count; i++) {
                fprintf(fp, "%s", sentences[i]);
                if (i < sentence_count - 1) fprintf(fp, " ");
            }
            fclose(fp);

            remove_lock(filename, sentence_num);
            char done[] = "Write Successful!\n";
            send(client_sock, done, strlen(done), 0);
            break;
        }

        // Parse "<word_index> <content...>"
        int index;
        char insert_text[512] = {0};
        if (sscanf(recv_buf, "%d %[^\n]", &index, insert_text) < 2) {
            char err[] = "ERROR: Invalid format. Use '<word_index> <content>' or 'ETIRW'.\n";
            send(client_sock, err, strlen(err), 0);
            continue;
        }

        // Split into words
        char words[MAX_WORDS][128];
        int word_count = split_words(working_sentence, words);

        if (index < 0 || index > word_count) {
            char err[] = "ERROR: Word index out of range.\n";
            send(client_sock, err, strlen(err), 0);
            continue;
        }

        // Insert new word(s) at position <index>
        char new_sentence[MAX_SENT_LEN] = "";
        for (int i = 0; i < index; i++) {
            strcat(new_sentence, words[i]);
            strcat(new_sentence, " ");
        }
        strcat(new_sentence, insert_text);
        if (index < word_count) strcat(new_sentence, " ");
        for (int i = index; i < word_count; i++) {
            strcat(new_sentence, words[i]);
            if (i < word_count - 1) strcat(new_sentence, " ");
        }

        // Now handle sentence splitting if new delimiters introduced
        char temp_sentences[MAX_SENTENCES][MAX_SENT_LEN];
        int new_count = split_sentences(new_sentence, temp_sentences);

        if (new_count > 1) {
            // Replace the current sentence with the split results
            // Shift sentences to make space
            for (int i = sentence_count - 1; i >= sentence_num + 1; i--) {
                strcpy(sentences[i + new_count - 1], sentences[i]);
            }
            for (int j = 0; j < new_count; j++) {
                strcpy(sentences[sentence_num + j], temp_sentences[j]);
            }
            sentence_count += (new_count - 1);
            strcpy(working_sentence, sentences[sentence_num]); // keep editing current one
        } else {
            strcpy(working_sentence, new_sentence);
        }

        char ok[] = "Update applied successfully.\n";
        send(client_sock, ok, strlen(ok), 0);
    }
}
