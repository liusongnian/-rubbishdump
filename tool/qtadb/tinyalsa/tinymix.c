/* tinymix.c
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include "cJSON.h"

static void tinymix_list_controls(struct mixer *mixer);
static void tinymix_list_json_controls(struct mixer *mixer);
static int tinymix_detail_control(struct mixer *mixer, const char *control,
                                  int prefix, int print_all);
static int tinymix_detail_json_control(struct mixer *mixer, const char *control,
                                  int prefix, int print_all,cJSON *jsoncontrol);
static int tinymix_set_value(struct mixer *mixer, const char *control,
                             char **values, unsigned int num_values);
static void tinymix_print_enum(struct mixer_ctl *ctl, const char *space,
                               int print_all);
static void tinymix_print_json_enum(struct mixer_ctl *ctl, const char *space,
                               int print_all,cJSON *jsoncontrol);

static const char *tinymix_short_options = "D:atvh";
static struct option tinymix_long_options[] = {
    {"device",	   required_argument, 0, 'D'},
    {"all-values", no_argument,       0, 'a'},
    {"tabs-only",  no_argument,       0, 't'},
    {"value-only", no_argument,       0, 'v'},
    {"help",       no_argument,       0, 'h'},
    {0,            0,                 0, 0}
};

static int g_tabs_only = 0;
static int g_all_values = 0;
static int g_value_only = 0;

static void usage (void) {
    fprintf(stderr,
"tinymix [options] [control name/#] [value to set]\n"
"    options:\n"
"    --device|-D <card#>   - use the given card # instead of 0.\n"
"    --all-values|-a       - show all possible values/ranges for control.\n"
"    --tabs-only|-t        - separate all output columns/values with tabs.\n"
"    --value-only|-v       - show only the value for the selected control.\n"
            );
}

int main(int argc, char **argv)
{
    struct mixer *mixer;
    int card = 0;
    int ret = 0;
    //testmain();
    while (1) {
        int option_index = 0;
        int option_char = 0;

        option_char = getopt_long(argc, argv, tinymix_short_options,
                                  tinymix_long_options, &option_index);
        if (option_char == -1)
            break;

        switch (option_char) {
        case 'D':
            card = atoi(optarg);
            break;
        case 'a':
            g_all_values = 1;
            break;
        case 't':
            g_tabs_only = 1;
            break;
        case 'v':
            g_value_only = 1;
            break;
        case 'h':
            usage();
            return 0;
        default:
            usage();
            return EINVAL;
        }
    }

    mixer = mixer_open(card);
    if (!mixer) {
        fprintf(stderr, "Failed to open mixer\n");
        return ENODEV;
    }

    if (argc == optind) {
        //printf("liusongnian Mixer name: '%s'\n", mixer_get_name(mixer));
        //tinymix_list_controls(mixer);
        tinymix_list_json_controls(mixer);
    } else if (argc == optind + 1) {
        ret = tinymix_detail_control(mixer, argv[optind], !g_value_only, !g_value_only);
    } else if (argc >= optind + 2) {
        ret = tinymix_set_value(mixer, argv[optind], &argv[optind + 1], argc - optind - 1);
    }else{
    	tinymix_list_controls(mixer);
    	tinymix_list_json_controls(mixer);
    }

    mixer_close(mixer);

    return ret;
}

static int isnumber(const char *str) {
    char *end;

    if (str == NULL || strlen(str) == 0)
        return 0;

    strtol(str, &end, 0);
    return strlen(end) == 0;
}

static void tinymix_list_controls(struct mixer *mixer)
{
    struct mixer_ctl *ctl;
    const char *name, *type;
    unsigned int num_ctls, num_values;
    unsigned int i;

    num_ctls = mixer_get_num_ctls(mixer);

    printf("Number of controls: %u\n", num_ctls);

    if (g_tabs_only)
        printf("ctl\ttype\tnum\tname\tvalue");
    else
        printf("ctl\ttype\tnum\t%-40s value\n", "name");
    if (g_all_values)
        printf("\trange/values\n");
    else
        printf("\n");
    for (i = 0; i < num_ctls; i++) {
        ctl = mixer_get_ctl(mixer, i);

        name = mixer_ctl_get_name(ctl);
        type = mixer_ctl_get_type_string(ctl);
        num_values = mixer_ctl_get_num_values(ctl);
        if (g_tabs_only)
            printf("%d\t%s\t%d\t%s\t", i, type, num_values, name);
        else
            printf("%d\t%s\t%d\t%-40s ", i, type, num_values, name);
        tinymix_detail_control(mixer, name, 0, g_all_values);
    }
}

static int print_preallocated(cJSON *root)
{
    /* declarations */
    char *out = NULL;
    char *buf = NULL;
    char *buf_fail = NULL;
    size_t len = 0;
    size_t len_fail = 0;

    /* formatted print */
    out = cJSON_Print(root);

    /* create buffer to succeed */
    /* the extra 5 bytes are because of inaccuracies when reserving memory */
    len = strlen(out) + 5;
    buf = (char*)malloc(len);
    if (buf == NULL)
    {
        printf("Failed to allocate memory.\n");
        exit(1);
    }

    /* create buffer to fail */
    len_fail = strlen(out);
    buf_fail = (char*)malloc(len_fail);
    if (buf_fail == NULL)
    {
        printf("Failed to allocate memory.\n");
        exit(1);
    }

    /* Print to buffer */
    if (!cJSON_PrintPreallocated(root, buf, (int)len, 1)) {
        printf("cJSON_PrintPreallocated failed!\n");
        if (strcmp(out, buf) != 0) {
            printf("cJSON_PrintPreallocated not the same as cJSON_Print!\n");
            printf("cJSON_Print result:\n%s\n", out);
            printf("cJSON_PrintPreallocated result:\n%s\n", buf);
        }
        free(out);
        free(buf_fail);
        free(buf);
        return -1;
    }

    /* success */
    printf("%s\n", buf);

    /* force it to fail */
    if (cJSON_PrintPreallocated(root, buf_fail, (int)len_fail, 1)) {
        printf("cJSON_PrintPreallocated failed to show error with insufficient memory!\n");
        printf("cJSON_Print result:\n%s\n", out);
        printf("cJSON_PrintPreallocated result:\n%s\n", buf_fail);
        free(out);
        free(buf_fail);
        free(buf);
        return -1;
    }

    free(out);
    free(buf_fail);
    free(buf);
    return 0;
}

static void tinymix_list_json_controls(struct mixer *mixer)
{
    struct mixer_ctl *ctl;
    const char *name, *type;
    unsigned int num_ctls, num_values;
    unsigned int i;
    static char control_num[15]={0};
    cJSON *root = NULL;
    cJSON *controls = NULL;
    cJSON *control = NULL;
    num_ctls = mixer_get_num_ctls(mixer);
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "card info", cJSON_CreateString(mixer_get_name(mixer)));
    cJSON_AddNumberToObject(root, "Number of controls", num_ctls);
    cJSON_AddItemToObject(root, "controls", controls = cJSON_CreateObject());

    for (i = 0; i < num_ctls; i++) {
        ctl = mixer_get_ctl(mixer, i);

        name = mixer_ctl_get_name(ctl);
        type = mixer_ctl_get_type_string(ctl);
        num_values = mixer_ctl_get_num_values(ctl);
        sprintf(control_num,"control%d",i);
        cJSON_AddItemToObject(controls,control_num, control = cJSON_CreateObject());
        cJSON_AddStringToObject(control, "type", type);
        cJSON_AddNumberToObject(control, "num_values", num_values);
        cJSON_AddStringToObject(control, "name", name);
        tinymix_detail_json_control(mixer, name, 0, g_all_values,control);
    }
        /* Print to text */
    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
    }
}

static void tinymix_print_enum(struct mixer_ctl *ctl, const char *space,
                               int print_all)
{
    unsigned int num_enums;
    unsigned int i;
    const char *string;
    int control_value = mixer_ctl_get_value(ctl, 0);

    if (print_all) {
        num_enums = mixer_ctl_get_num_enums(ctl);
        for (i = 0; i < num_enums; i++) {
            string = mixer_ctl_get_enum_string(ctl, i);
            printf("%s%s%s",
                   control_value == (int)i ? ">" : "", string,
                   (i < num_enums - 1) ? space : "");
        }
    }
    else {
        string = mixer_ctl_get_enum_string(ctl, control_value);
        printf("%s", string);
    }
}

static void tinymix_print_json_enum(struct mixer_ctl *ctl, const char *space,
                               int print_all,cJSON *jsoncontrol)
{
    unsigned int num_enums;
    unsigned int i;
    const char *string;
    static char buf_tmp[128]={0};
    static char value_tmp[128]={0};
    int control_value = mixer_ctl_get_value(ctl, 0);

    if (print_all) {
        num_enums = mixer_ctl_get_num_enums(ctl);
        for (i = 0; i < num_enums; i++) {
            sprintf(value_tmp,"value%d",i);
            string = mixer_ctl_get_enum_string(ctl, i);
            sprintf(buf_tmp,"%s%s%s",
                   control_value == (int)i ? ">" : "", string,
                   (i < num_enums - 1) ? space : "");
            cJSON_AddStringToObject(jsoncontrol,value_tmp, buf_tmp);     
        }
    }
    else {
        string = mixer_ctl_get_enum_string(ctl, control_value);
        //printf("%s", string);
        cJSON_AddStringToObject(jsoncontrol, "value0", string);
    }
}

static int tinymix_detail_control(struct mixer *mixer, const char *control,
                                  int prefix, int print_all)
{
    struct mixer_ctl *ctl;
    enum mixer_ctl_type type;
    unsigned int num_values;
    unsigned int i;
    int min, max;
    int ret;
    char *buf = NULL;
    size_t len;
    unsigned int tlv_header_size = 0;
    const char *space = g_tabs_only ? "\t" : " ";

    if (isnumber(control))
        ctl = mixer_get_ctl(mixer, atoi(control));
    else
        ctl = mixer_get_ctl_by_name(mixer, control);

    if (!ctl) {
        fprintf(stderr, "Invalid mixer control: %s\n", control);
        return ENOENT;
    }

    type = mixer_ctl_get_type(ctl);
    num_values = mixer_ctl_get_num_values(ctl);

    if (type == MIXER_CTL_TYPE_BYTE) {
        if (mixer_ctl_is_access_tlv_rw(ctl)) {
            tlv_header_size = TLV_HEADER_SIZE;
        }
        buf = calloc(1, num_values + tlv_header_size);
        if (buf == NULL) {
            fprintf(stderr, "Failed to alloc mem for bytes %d\n", num_values);
            return ENOENT;
        }

        len = num_values;
        ret = mixer_ctl_get_array(ctl, buf, len + tlv_header_size);
        if (ret < 0) {
            fprintf(stderr, "Failed to mixer_ctl_get_array\n");
            free(buf);
            return ENOENT;
        }
    }

    if (prefix)
        printf("%s:%s", mixer_ctl_get_name(ctl), space);

    for (i = 0; i < num_values; i++) {
        switch (type)
        {
        case MIXER_CTL_TYPE_INT:
            printf("%d", mixer_ctl_get_value(ctl, i));
            break;
        case MIXER_CTL_TYPE_BOOL:
            printf("%s", mixer_ctl_get_value(ctl, i) ? "On" : "Off");
            break;
        case MIXER_CTL_TYPE_ENUM:
            tinymix_print_enum(ctl, space, print_all);
            break;
        case MIXER_CTL_TYPE_BYTE:
            /* skip printing TLV header if exists */
            printf(" %02x", buf[i + tlv_header_size]);
            break;
        default:
            printf("unknown");
            break;
        }

        if (i < num_values - 1)
            printf("%s", space);
    }

    if (print_all) {
        if (type == MIXER_CTL_TYPE_INT) {
            min = mixer_ctl_get_range_min(ctl);
            max = mixer_ctl_get_range_max(ctl);
            printf("%s(dsrange %d->%d)", space, min, max);
        }
    }

    free(buf);

    printf("\n");
    return 0;
}

static int tinymix_detail_json_control(struct mixer *mixer, const char *control,
                                  int prefix, int print_all,cJSON *jsoncontrol)
{
    struct mixer_ctl *ctl;
    enum mixer_ctl_type type;
    unsigned int num_values;
    unsigned int i;
    int min, max;
    static char err_info[128];
    static char buf_tmp[128];
    static char value_tmp[128];
    int ret;
    char *buf = NULL;
    size_t len;
    unsigned int tlv_header_size = 0;
    const char *space = g_tabs_only ? "\t" : " ";

    if (isnumber(control))
        ctl = mixer_get_ctl(mixer, atoi(control));
    else
        ctl = mixer_get_ctl_by_name(mixer, control);

    if (!ctl) {
        sprintf(err_info, "Invalid mixer control: %s", control);
        cJSON_AddStringToObject(jsoncontrol, "value0",err_info);
        return ENOENT;
    }

    type = mixer_ctl_get_type(ctl);
    num_values = mixer_ctl_get_num_values(ctl);

    if (type == MIXER_CTL_TYPE_BYTE) {
        if (mixer_ctl_is_access_tlv_rw(ctl)) {
            tlv_header_size = TLV_HEADER_SIZE;
        }
        buf = calloc(1, num_values + tlv_header_size);
        if (buf == NULL) {
            sprintf(err_info, "Failed to alloc mem for bytes %d\n", num_values);
            cJSON_AddStringToObject(jsoncontrol, "value0",err_info);
            return ENOENT;
        }

        len = num_values;
        ret = mixer_ctl_get_array(ctl, buf, len + tlv_header_size);
        if (ret < 0) {
            sprintf(err_info,"Failed to mixer_ctl_get_array");
            cJSON_AddStringToObject(jsoncontrol, "value0",err_info);
            free(buf);
            return ENOENT;
        }
    }

    if (prefix)
        printf("%s:%s", mixer_ctl_get_name(ctl), space);

    for (i = 0; i < num_values; i++) {
        sprintf(value_tmp,"value%d",i);
        switch (type)
        {
        case MIXER_CTL_TYPE_INT:
            //printf("%d", mixer_ctl_get_value(ctl, i));
            cJSON_AddNumberToObject(jsoncontrol, value_tmp,mixer_ctl_get_value(ctl, i));
            break;
        case MIXER_CTL_TYPE_BOOL:
            //printf("%s", mixer_ctl_get_value(ctl, i) ? "On" : "Off");
            sprintf(buf_tmp,"%s", mixer_ctl_get_value(ctl, i) ? "On" : "Off");
            cJSON_AddStringToObject(jsoncontrol,value_tmp,buf_tmp);
            break;
        case MIXER_CTL_TYPE_ENUM:
            tinymix_print_json_enum(ctl, space, print_all,jsoncontrol);
            break;
        case MIXER_CTL_TYPE_BYTE:
            /* skip printing TLV header if exists */
            sprintf(buf_tmp," %02x", buf[i + tlv_header_size]);
            cJSON_AddStringToObject(jsoncontrol, value_tmp,buf_tmp);
            break;
        default:
            //printf("unknown");
            cJSON_AddStringToObject(jsoncontrol, value_tmp,"unknown");
            break;
        }
    }

    if (print_all) {
        if (type == MIXER_CTL_TYPE_INT) {
            min = mixer_ctl_get_range_min(ctl);
            max = mixer_ctl_get_range_max(ctl);
            sprintf(buf_tmp,"%s(dsrange %d->%d)", space, min, max);
            cJSON_AddStringToObject(jsoncontrol, "value1",buf_tmp);
        }
    }

    free(buf);
    return 0;
}

static void tinymix_set_byte_ctl(struct mixer_ctl *ctl,
    char **values, unsigned int num_values)
{
    int ret;
    char *buf;
    char *end;
    unsigned int i;
    long n;
    unsigned int *tlv, tlv_size;
    unsigned int tlv_header_size = 0;

    if (mixer_ctl_is_access_tlv_rw(ctl)) {
        tlv_header_size = TLV_HEADER_SIZE;
    }

    tlv_size = num_values + tlv_header_size;

    buf = calloc(1, tlv_size);
    if (buf == NULL) {
        fprintf(stderr, "set_byte_ctl: Failed to alloc mem for bytes %d\n", num_values);
        exit(EXIT_FAILURE);
    }

    tlv = (unsigned int *)buf;
    tlv[0] = 0;
    tlv[1] = num_values;

    for (i = 0; i < num_values; i++) {
        errno = 0;
        n = strtol(values[i], &end, 0);
        if (*end) {
            fprintf(stderr, "%s not an integer\n", values[i]);
            goto fail;
        }
        if (errno) {
            fprintf(stderr, "strtol: %s: %s\n", values[i],
                strerror(errno));
            goto fail;
        }
        if (n < 0 || n > 0xff) {
            fprintf(stderr, "%s should be between [0, 0xff]\n",
                values[i]);
            goto fail;
        }
        /* start filling after the TLV header */
        buf[i + tlv_header_size] = n;
    }

    ret = mixer_ctl_set_array(ctl, buf, tlv_size);
    if (ret < 0) {
        fprintf(stderr, "Failed to set binary control\n");
        goto fail;
    }

    free(buf);
    return;

fail:
    free(buf);
    exit(EXIT_FAILURE);
}

static int tinymix_set_value(struct mixer *mixer, const char *control,
                             char **values, unsigned int num_values)
{
    struct mixer_ctl *ctl;
    enum mixer_ctl_type type;
    unsigned int num_ctl_values;
    unsigned int i;

    if (isnumber(control))
        ctl = mixer_get_ctl(mixer, atoi(control));
    else
        ctl = mixer_get_ctl_by_name(mixer, control);

    if (!ctl) {
        fprintf(stderr, "Invalid mixer control: %s\n", control);
        return ENOENT;
    }

    type = mixer_ctl_get_type(ctl);
    num_ctl_values = mixer_ctl_get_num_values(ctl);

    if (type == MIXER_CTL_TYPE_BYTE) {
        tinymix_set_byte_ctl(ctl, values, num_values);
        return ENOENT;
    }

    if (isnumber(values[0])) {
        if (num_values == 1) {
            /* Set all values the same */
            int value = atoi(values[0]);

            for (i = 0; i < num_ctl_values; i++) {
                if (mixer_ctl_set_value(ctl, i, value)) {
                    fprintf(stderr, "Error: invalid value\n");
                    return EINVAL;
                }
            }
        } else {
            /* Set multiple values */
            if (num_values > num_ctl_values) {
                fprintf(stderr,
                        "Error: %u values given, but control only takes %u\n",
                        num_values, num_ctl_values);
                return EINVAL;
            }
            for (i = 0; i < num_values; i++) {
                if (mixer_ctl_set_value(ctl, i, atoi(values[i]))) {
                    fprintf(stderr, "Error: invalid value for index %d\n", i);
                    return EINVAL;
                }
            }
        }
    } else {
        if (type == MIXER_CTL_TYPE_ENUM) {
            if (num_values != 1) {
                fprintf(stderr, "Enclose strings in quotes and try again\n");
                return EINVAL;
            }
            if (mixer_ctl_set_enum_by_string(ctl, values[0])) {
                fprintf(stderr, "Error: invalid enum value\n");
                return EINVAL;
            }
        } else {
            fprintf(stderr, "Error: only enum types can be set with strings\n");
            return EINVAL;
        }
    }

    return 0;
}
