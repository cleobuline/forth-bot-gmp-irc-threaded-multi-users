#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <curl/curl.h>
#include "memory_forth.h"
#include "forth_bot.h"
 

char *remove_slashes(const char *input) {
    if (!input) return NULL;
    char *clean = malloc(strlen(input) + 1);
    if (!clean) return NULL;
    char *out = clean;
    const char *in = input;
    while (*in) {
        if (*in == '\\' && *(in + 1) == '/') {
            *out++ = '/';
            in += 2;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
    return clean;
}

// Callback pour les données textuelles (JSON)
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    char **data = (char **)userp;
    size_t realsize = size * nmemb;
    char *temp = realloc(*data, (*data ? strlen(*data) : 0) + realsize + 1);
    if (!temp) return 0;
    *data = temp;
    if (!strlen(*data)) *data[0] = '\0';
    strncat(*data, contents, realsize);
    return realsize;
}

// Callback pour les données binaires (image)
size_t write_binary_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *file = (FILE *)userp;
    return fwrite(contents, size, nmemb, file);
}


char *generate_image(const char *prompt) {
    if (!prompt) {
        send_to_channel("DEBUG: Prompt is NULL");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        send_to_channel("DEBUG: Failed to init curl");
        return NULL;
    }

    char *response_data = malloc(1);
    if (!response_data) {
        send_to_channel("DEBUG: Failed to malloc response_data");
        curl_easy_cleanup(curl);
        return NULL;
    }
    response_data[0] = '\0';

    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "{\"model\":\"dall-e-3\",\"prompt\":\"%s\",\"n\":1,\"size\":\"1024x1024\"}", prompt);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Authorization: Bearer PUT HERE YOUR OPENAI API KEY");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/images/generations");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        send_to_channel("DEBUG: OpenAI request failed");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *url_key = strstr(response_data, "\"url\":");
    if (!url_key) {
        send_to_channel("DEBUG: No 'url' key found in OpenAI response");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *url_start = url_key + 6;
    while (*url_start && (*url_start == ' ' || *url_start == ':' || *url_start == '"')) url_start++;
    char *url_end = strchr(url_start, '"');
    if (!url_end || url_end <= url_start) {
        send_to_channel("DEBUG: Malformed URL in OpenAI response");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    size_t url_len = url_end - url_start;
    char *long_url = malloc(url_len + 1);
    if (!long_url) {
        send_to_channel("DEBUG: Failed to allocate memory for long URL");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }
    strncpy(long_url, url_start, url_len);
    long_url[url_len] = '\0';
    free(response_data);
    curl_slist_free_all(headers);

    char temp_filename[] = "/tmp/mforth_image_XXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        send_to_channel("DEBUG: Failed to create temp file");
        free(long_url);
        curl_easy_cleanup(curl);
        return NULL;
    }
    FILE *temp_file = fdopen(fd, "wb");
    if (!temp_file) {
        send_to_channel("DEBUG: Failed to open temp file");
        free(long_url);
        close(fd);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, long_url);
    curl_easy_setopt(curl, CURLOPT_POST, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_binary_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, temp_file);

    res = curl_easy_perform(curl);
    fclose(temp_file);
    if (res != CURLE_OK) {
        send_to_channel("DEBUG: Failed to download image from OpenAI");
        free(long_url);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }
    free(long_url);

    char *imgbb_response = malloc(1);
    if (!imgbb_response) {
        send_to_channel("DEBUG: Failed to malloc imgbb_response");
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }
    imgbb_response[0] = '\0';

    CURL *curl_imgbb = curl_easy_init();
    if (!curl_imgbb) {
        send_to_channel("DEBUG: Failed to init curl for ImgBB");
        free(imgbb_response);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }

    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "image",
                 CURLFORM_FILE, temp_filename,
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "key",
                 CURLFORM_COPYCONTENTS, "PUT HERE YOUR OMGBB API KEY",
                 CURLFORM_END);

    curl_easy_setopt(curl_imgbb, CURLOPT_URL, "https://api.imgbb.com/1/upload");
    curl_easy_setopt(curl_imgbb, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl_imgbb, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_imgbb, CURLOPT_WRITEDATA, &imgbb_response);

    res = curl_easy_perform(curl_imgbb);
    if (res != CURLE_OK) {
        send_to_channel("DEBUG: ImgBB upload failed");
        free(imgbb_response);
        curl_formfree(formpost);
        curl_easy_cleanup(curl_imgbb);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *imgbb_url_key = strstr(imgbb_response, "\"url\":");
    if (!imgbb_url_key) {
        send_to_channel("DEBUG: No 'url' key in ImgBB response");
        free(imgbb_response);
        curl_formfree(formpost);
        curl_easy_cleanup(curl_imgbb);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *imgbb_url_start = imgbb_url_key + 6;
    while (*imgbb_url_start && (*imgbb_url_start == ' ' || *imgbb_url_start == ':' || *imgbb_url_start == '"')) imgbb_url_start++;
    char *imgbb_url_end = strchr(imgbb_url_start, '"');
    if (!imgbb_url_end || imgbb_url_end <= imgbb_url_start) {
        send_to_channel("DEBUG: Malformed URL in ImgBB response");
        free(imgbb_response);
        curl_formfree(formpost);
        curl_easy_cleanup(curl_imgbb);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }

    size_t imgbb_url_len = imgbb_url_end - imgbb_url_start;
    char *raw_imgbb_url = malloc(imgbb_url_len + 1);
    if (!raw_imgbb_url) {
        send_to_channel("DEBUG: Failed to allocate memory for raw ImgBB URL");
        free(imgbb_response);
        curl_formfree(formpost);
        curl_easy_cleanup(curl_imgbb);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }
    strncpy(raw_imgbb_url, imgbb_url_start, imgbb_url_len);
    raw_imgbb_url[imgbb_url_len] = '\0';

    char *imgbb_url = remove_slashes(raw_imgbb_url);
    if (!imgbb_url) {
        send_to_channel("DEBUG: Failed to clean ImgBB URL");
        free(raw_imgbb_url);
        free(imgbb_response);
        curl_formfree(formpost);
        curl_easy_cleanup(curl_imgbb);
        unlink(temp_filename);
        curl_easy_cleanup(curl);
        return NULL;
    }

    free(raw_imgbb_url);
    free(imgbb_response);
    curl_formfree(formpost);
    curl_easy_cleanup(curl_imgbb);
    unlink(temp_filename);
    curl_easy_cleanup(curl);

    return imgbb_url;
}

char *generate_image_tiny(const char *prompt) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        send_to_channel("DEBUG: Failed to init curl");
        return NULL;
    }

    char *response_data = malloc(1);
    response_data[0] = '\0';
    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "{\"model\":\"dall-e-3\",\"prompt\":\"%s\",\"n\":1,\"size\":\"1024x1024\"}", prompt);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Authorization: Bearer PUT HERE YOUR OPEN AI API KEY");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/images/generations");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *url_key = strstr(response_data, "\"url\":");
    if (!url_key) {
        send_to_channel("DEBUG: No 'url' key found in OpenAI response");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *url_start = url_key + 6;
    while (*url_start && (*url_start == ' ' || *url_start == ':' || *url_start == '"')) {
        url_start++;
    }

    char *url_end = strchr(url_start, '"');
    if (!url_end) {
        send_to_channel("DEBUG: Malformed URL in OpenAI response (no closing quote)");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    size_t url_len = url_end - url_start;
    char *long_url = malloc(url_len + 1);
    if (!long_url) {
        send_to_channel("DEBUG: Failed to allocate memory for long URL");
        free(response_data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }
    strncpy(long_url, url_start, url_len);
    long_url[url_len] = '\0';

    free(response_data);
    curl_slist_free_all(headers);

    char *short_url_data = malloc(1);
    short_url_data[0] = '\0';
    char tinyurl_request[2048];
    snprintf(tinyurl_request, sizeof(tinyurl_request), "https://tinyurl.com/api-create.php?url=%s", long_url);

    curl_easy_setopt(curl, CURLOPT_URL, tinyurl_request);
    curl_easy_setopt(curl, CURLOPT_POST, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &short_url_data);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg), "DEBUG: TinyURL request failed: %s", curl_easy_strerror(res));
        send_to_channel(debug_msg);
        free(long_url);
        free(short_url_data);
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (strncmp(short_url_data, "https://tinyurl.com/", 20) != 0) {
        send_to_channel("DEBUG: TinyURL returned an invalid response");
        free(long_url);
        free(short_url_data);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *short_url = strdup(short_url_data);

    free(long_url);
    free(short_url_data);
    curl_easy_cleanup(curl);

    return short_url;
}
