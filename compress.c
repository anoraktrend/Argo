/*
 * ccompress - Create self-extracting C archives using LZ77 compression
 *
 * Usage: ccompress [-o output.c] file1 [file2 ...]
 *
 * Generates a C source file which, when compiled/interpreted,
 * decompresses and restores the original files.
 *
 * The output is valid C++ and works with C++ interpreters (Cling, Cint).
 *
 * LZ77 details:
 *   - 12-bit sliding window (4096 bytes)
 *   - 4-bit match length (3-18 bytes)
 *   - Bit-packed control bytes (8 items per block)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__MINGW32__)
  static char *strdup(const char *s) {
      size_t n = strlen(s) + 1;
      char *p = (char *)malloc(n);
      if (p) memcpy(p, s, n);
      return p;
  }
  #if defined(_MSC_VER) && _MSC_VER < 1900
    #define snprintf _snprintf
  #endif
#endif

#define WINDOW_BITS    12
#define WINDOW_SIZE    (1 << WINDOW_BITS)
#define MIN_MATCH      3
#define MAX_MATCH      18
#define HASH_BITS      12
#define HASH_SIZE      (1 << HASH_BITS)
#define MAX_CHAIN      128
#define MAX_FILES      256
#define OUT_BUF_GROW   (64 * 1024)

typedef struct {
    char     *name;
    size_t    name_len;
    unsigned char *data;
    size_t    size;
} FileEntry;

typedef struct {
    unsigned char *data;
    size_t        size;
    size_t        cap;
} ByteBuf;

static int hash_func(const unsigned char *p)
{
    return ((int)(p[0] << 8) ^ (int)(p[1] << 4) ^ (int)p[2]) & (HASH_SIZE - 1);
}

static void bytebuf_init(ByteBuf *b)
{
    b->data = NULL;
    b->size = 0;
    b->cap  = 0;
}

static void bytebuf_append(ByteBuf *b, unsigned char byte)
{
    if (b->size >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : OUT_BUF_GROW;
        b->data = realloc(b->data, b->cap);
        if (!b->data) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
    b->data[b->size++] = byte;
}

static int find_best_match(const unsigned char *data, size_t pos,
                            size_t size, const int *head, const int *next,
                            int *out_len)
{
    int h = hash_func(data + pos);
    int best_len  = MIN_MATCH - 1;
    int best_off  = 0;
    int chain     = head[h];
    int look      = MAX_CHAIN;

    while (chain >= 0 && look-- > 0) {
        size_t dist = pos - (size_t)chain;
        if (dist > WINDOW_SIZE)
            break;
        if (chain < (int)pos) {
            int len = 0;
            while (len < MAX_MATCH && pos + (size_t)len < size &&
                   data[(size_t)chain + (size_t)len] == data[pos + (size_t)len])
                len++;
            if (len > best_len) {
                best_len = len;
                best_off = (int)dist;
                if (best_len == MAX_MATCH)
                    break;
            }
        }
        chain = next[chain];
    }

    if (best_len >= MIN_MATCH) {
        *out_len = best_len;
        return best_off;
    }
    return -1;
}

static int *build_hash_chains(const unsigned char *data, size_t size,
                               int *head)
{
    int *next = malloc(size * sizeof(int));
    if (!next) return NULL;

    for (size_t i = 0; i + 2 < size; i++) {
        int h = hash_func(data + i);
        next[i] = head[h];
        head[h] = (int)i;
    }
    return next;
}

static void compress_buf(const unsigned char *in, size_t in_size,
                          ByteBuf *out)
{
    if (in_size == 0) return;

    int head[HASH_SIZE];
    memset(head, 0xff, sizeof(head));

    int *next = build_hash_chains(in, in_size, head);
    if (!next) {
        fprintf(stderr, "Failed to allocate hash chains\n");
        exit(1);
    }

    size_t pos = 0;
    unsigned char flags = 0;
    int bit = 0;
    size_t flag_pos = 0;

    bytebuf_append(out, 0);
    flag_pos = out->size - 1;

    while (pos < in_size) {
        int match_len;
        int match_off = find_best_match(in, pos, in_size, head, next,
                                         &match_len);

        if (match_off > 0 && match_len >= MIN_MATCH) {
            flags |= (unsigned char)(1 << bit);
            int enc = ((match_len - MIN_MATCH) << 4) | (match_off >> 8);
            bytebuf_append(out, (unsigned char)(enc & 0xff));
            bytebuf_append(out, (unsigned char)(match_off & 0xff));
            pos += (size_t)match_len;
        } else {
            bytebuf_append(out, in[pos]);
            pos++;
        }

        bit++;
        if (bit == 8) {
            out->data[flag_pos] = flags;
            flags = 0;
            bit = 0;
            bytebuf_append(out, 0);
            flag_pos = out->size - 1;
        }
    }

    if (bit > 0) {
        out->data[flag_pos] = flags;
    } else {
        out->size--;
    }

    free(next);
}

static void write_escaped_string(FILE *out, const char *str, size_t len)
{
    putc('"', out);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
        case '\a': fputs("\\a", out);  break;
        case '\b': fputs("\\b", out);  break;
        case '\f': fputs("\\f", out);  break;
        case '\n': fputs("\\n", out);  break;
        case '\r': fputs("\\r", out);  break;
        case '\t': fputs("\\t", out);  break;
        case '\v': fputs("\\v", out);  break;
        case '\\': fputs("\\\\", out); break;
        case '"':  fputs("\\\"", out); break;
        default:
            if (c < 32 || c > 126)
                fprintf(out, "\\x%02x", c);
            else
                putc((char)c, out);
            break;
        }
    }
    putc('"', out);
}

static void write_byte_array(FILE *out, const char *name,
                              const unsigned char *data, size_t size)
{
    fprintf(out, "static const unsigned char %s[%zu] = {\n", name, size);
    for (size_t i = 0; i < size; i++) {
        if (i % 12 == 0) fputs("    ", out);
        fprintf(out, "0x%02x", data[i]);
        if (i + 1 < size) fputs(", ", out);
        if (i % 12 == 11 || i + 1 == size) putc('\n', out);
    }
    fputs("};\n\n", out);
}

static int write_output(const char *output_path,
                         FileEntry *files, int num_files,
                         ByteBuf *compressed, size_t *orig_sizes,
                         size_t *comp_sizes)
{
    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Failed to open output file: %s\n", output_path);
        return 1;
    }

    fputs("/* Auto-generated by ccompress - Self-extracting archive */\n", out);
    fputs("/* Compile: cc -o extract this_file.c && ./extract */\n", out);
    fputs("/* Or run directly: cling this_file.c */\n\n", out);
    fputs("#include <stdio.h>\n", out);
    fputs("#include <stdlib.h>\n", out);
    fputs("#include <string.h>\n\n", out);
    fputs("#define MIN_MATCH 3\n", out);
    fputs("#define WINDOW_SIZE 4096\n\n", out);

    int file_idx;
    for (file_idx = 0; file_idx < num_files; file_idx++) {
        char arr_name[64];
        snprintf(arr_name, sizeof(arr_name), "comp_%d", file_idx);

        size_t offset = 0;
        for (int j = 0; j < file_idx; j++)
            offset += comp_sizes[j];

        write_byte_array(out, arr_name,
                         compressed->data + offset, comp_sizes[file_idx]);
    }

    fputs("static const char *filenames[] = {\n", out);
    for (file_idx = 0; file_idx < num_files; file_idx++) {
        fputs("    ", out);
        write_escaped_string(out, files[file_idx].name,
                              files[file_idx].name_len);
        fputs(",\n", out);
    }
    fputs("};\n\n", out);

    fputs("static const unsigned char *comp_data[] = {\n", out);
    for (file_idx = 0; file_idx < num_files; file_idx++) {
        fprintf(out, "    comp_%d,\n", file_idx);
    }
    fputs("};\n\n", out);

    fputs("static const size_t comp_sizes[] = {\n", out);
    for (file_idx = 0; file_idx < num_files; file_idx++) {
        fprintf(out, "    %zu,\n", comp_sizes[file_idx]);
    }
    fputs("};\n\n", out);

    fputs("static const size_t orig_sizes[] = {\n", out);
    for (file_idx = 0; file_idx < num_files; file_idx++) {
        fprintf(out, "    %zu,\n", orig_sizes[file_idx]);
    }
    fputs("};\n\n", out);

    fprintf(out, "static const int num_files = %d;\n\n", num_files);

    fputs("static size_t decompress(const unsigned char *in, "
           "size_t in_size,\n", out);
    fputs("                          unsigned char *out)\n", out);
    fputs("{\n", out);
    fputs("    size_t ip = 0, op = 0;\n", out);
    fputs("    while (ip < in_size) {\n", out);
    fputs("        unsigned char flags = in[ip++];\n", out);
    fputs("        for (int i = 0; i < 8 && ip < in_size; i++) {\n", out);
    fputs("            if (flags & (1 << i)) {\n", out);
    fputs("                unsigned char a = in[ip++];\n", out);
    fputs("                unsigned char b = in[ip++];\n", out);
    fputs("                int len = (a >> 4) + MIN_MATCH;\n", out);
    fputs("                int off = ((a & 15) << 8) | b;\n", out);
    fputs("                for (int j = 0; j < len; j++)\n", out);
    fputs("                    out[op + j] = out[op + j - off];\n", out);
    fputs("                op += (size_t)len;\n", out);
    fputs("            } else {\n", out);
    fputs("                out[op++] = in[ip++];\n", out);
    fputs("            }\n", out);
    fputs("        }\n", out);
    fputs("    }\n", out);
    fputs("    return op;\n", out);
    fputs("}\n\n", out);

    fputs("int main(void)\n", out);
    fputs("{\n", out);
    fputs("    int errors = 0;\n", out);
    fputs("    for (int i = 0; i < num_files; i++) {\n", out);
    fputs("        unsigned char *buf = (unsigned char *)"
           "malloc(orig_sizes[i]);\n", out);
    fputs("        if (!buf) {\n", out);
    fputs("            fprintf(stderr, \"Memory error\\n\");\n", out);
    fputs("            return 1;\n", out);
    fputs("        }\n", out);
    fputs("        size_t dec = decompress(comp_data[i], "
           "comp_sizes[i], buf);\n", out);
    fputs("        if (dec != orig_sizes[i]) {\n", out);
    fputs("            fprintf(stderr, \"Size mismatch for %s: "
           "expected %zu, got %zu\\n\",\n", out);
    fputs("                    filenames[i], orig_sizes[i], dec);\n", out);
    fputs("            free(buf);\n", out);
    fputs("            errors++;\n", out);
    fputs("            continue;\n", out);
    fputs("        }\n", out);
    fputs("        FILE *fp = fopen(filenames[i], \"wb\");\n", out);
    fputs("        if (!fp) {\n", out);
    fputs("            fprintf(stderr, \"Failed to create %s\\n\",\n",
           out);
    fputs("                    filenames[i]);\n", out);
    fputs("            free(buf);\n", out);
    fputs("            errors++;\n", out);
    fputs("            continue;\n", out);
    fputs("        }\n", out);
    fputs("        fwrite(buf, 1, dec, fp);\n", out);
    fputs("        fclose(fp);\n", out);
    fputs("        free(buf);\n", out);
    fputs("        printf(\"Extracted: %s (%zu bytes)\\n\",\n",
           out);
    fputs("               filenames[i], orig_sizes[i]);\n", out);
    fputs("    }\n", out);
    fputs("    printf(\"Done. %d file(s) extracted", out);
    fputs(" with %d error(s)\\n\",\n", out);
    fputs("           num_files, errors);\n", out);
    fputs("    return errors ? 1 : 0;\n", out);
    fputs("}\n", out);

    fclose(out);
    return 0;
}

int main(int argc, char **argv)
{
    const char *output_path = "self_extracting.c";
    const char *input_files[MAX_FILES];
    int num_inputs = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "-o requires a filename\n");
                return 1;
            }
            output_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [-o output.c] file1 [file2 ...]\n",
                    argv[0]);
            return 1;
        } else {
            if (num_inputs >= MAX_FILES) {
                fprintf(stderr, "Too many files (max %d)\n", MAX_FILES);
                return 1;
            }
            input_files[num_inputs++] = argv[i];
        }
    }

    if (num_inputs == 0) {
        fprintf(stderr, "Usage: %s [-o output.c] file1 [file2 ...]\n",
                argv[0]);
        return 1;
    }

    FileEntry files[MAX_FILES];
    ByteBuf compressed;
    bytebuf_init(&compressed);

    size_t orig_sizes[MAX_FILES];
    size_t comp_sizes[MAX_FILES];

    int file_idx;
    for (file_idx = 0; file_idx < num_inputs; file_idx++) {
        FILE *fp = fopen(input_files[file_idx], "rb");
        if (!fp) {
            fprintf(stderr, "Failed to open: %s\n",
                    input_files[file_idx]);
            return 1;
        }

        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        if (sz < 0) {
            fprintf(stderr, "Failed to read: %s\n",
                    input_files[file_idx]);
            fclose(fp);
            return 1;
        }
        rewind(fp);

        files[file_idx].name = strdup(input_files[file_idx]);
        files[file_idx].name_len = strlen(input_files[file_idx]);
        files[file_idx].size = (size_t)sz;
        files[file_idx].data = malloc((size_t)sz + 1);
        if (!files[file_idx].data) {
            fprintf(stderr, "Out of memory\n");
            fclose(fp);
            return 1;
        }

        size_t read = fread(files[file_idx].data, 1, (size_t)sz, fp);
        fclose(fp);

        if ((long)read != sz) {
            fprintf(stderr, "Short read: %s\n",
                    input_files[file_idx]);
            return 1;
        }

        orig_sizes[file_idx] = (size_t)sz;
    }

    size_t total_orig = 0;
    for (file_idx = 0; file_idx < num_inputs; file_idx++) {
        size_t start = compressed.size;

        if (files[file_idx].size > 0) {
            compress_buf(files[file_idx].data, files[file_idx].size,
                          &compressed);
        }

        comp_sizes[file_idx] = compressed.size - start;
        total_orig += orig_sizes[file_idx];

        printf("  %-30s %7zu -> %7zu bytes (%.1f%%)\n",
               files[file_idx].name,
               orig_sizes[file_idx],
               comp_sizes[file_idx],
               orig_sizes[file_idx] > 0
                 ? (100.0 * comp_sizes[file_idx] / orig_sizes[file_idx])
                 : 0.0);
    }

    printf("  %-30s %7zu -> %7zu bytes (%.1f%%)\n",
           "[total]", total_orig, compressed.size,
           total_orig > 0
             ? (100.0 * compressed.size / total_orig)
             : 0.0);

    int ret = write_output(output_path, files, num_inputs,
                            &compressed, orig_sizes, comp_sizes);
    if (ret == 0) {
        printf("Archive written to: %s\n", output_path);
        printf("Extract with: cc -o extract %s && ./extract\n",
               output_path);
    }

    for (file_idx = 0; file_idx < num_inputs; file_idx++) {
        free(files[file_idx].name);
        free(files[file_idx].data);
    }
    free(compressed.data);

    return ret;
}
