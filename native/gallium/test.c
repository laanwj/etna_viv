#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_iterate.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_memory.h"

#include "etna/isa.xml.h"

#include <stdio.h>

static char *
load_text_file(const char *file_name)
{
	char *text = NULL;
	size_t size;
	size_t total_read = 0;
	FILE *fp = fopen(file_name, "rb");

	if (!fp) {
		return NULL;
	}

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	text = (char *) malloc(size + 1);
	if (text != NULL) {
		do {
			size_t bytes = fread(text + total_read,
					     1, size - total_read, fp);
			if (bytes < size - total_read) {
				free(text);
				text = NULL;
				break;
			}

			if (bytes == 0) {
				break;
			}

			total_read += bytes;
		} while (total_read < size);

		text[total_read] = '\0';
	}

	fclose(fp);

	return text;
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "Must pass shader source name as argument\n");
        exit(1);
    }
    const char *text = load_text_file(argv[1]);
    if(!text)
    {
        fprintf(stderr, "Unable to open %s\n", argv[1]);
        exit(1);
    }
    struct tgsi_token tokens[1024];
    if(tgsi_text_translate(text, tokens, Elements(tokens)))
    {
        tgsi_dump(tokens, 0); 

        union {
            struct tgsi_header h;
            struct tgsi_token t;
        } hdr;

        hdr.t = tokens[0];
        printf("Header size: %i\n", hdr.h.HeaderSize);
        printf("Body size: %i\n", hdr.h.BodySize);
        int totalSize = hdr.h.HeaderSize + hdr.h.BodySize;

        for(int i=0; i<totalSize; ++i)
        {
            printf("%08x ", *((uint32_t*)&tokens[i]));
        }
        printf("\n");

        /* Parsing test */
        struct tgsi_parse_context ctx = {};
        unsigned status = TGSI_PARSE_OK;
        status = tgsi_parse_init(&ctx, tokens);
        assert(status == TGSI_PARSE_OK);

        while(!tgsi_parse_end_of_tokens(&ctx))
        {
            tgsi_parse_token(&ctx);
            /* declaration / immediate / instruction / property */
            printf("Parsed token! %i\n", ctx.FullToken.Token.Type);
        }

        tgsi_parse_free(&ctx);
    } else {
        fprintf(stderr, "Unable to parse %s\n", argv[1]);
    }

    return 0;
}

