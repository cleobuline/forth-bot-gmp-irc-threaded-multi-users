#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

size_t write_binary_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *file = (FILE *)userp;
    return fwrite(contents, size, nmemb, file);
}

 

char *generate_image(const char *prompt)
{
    if (!prompt) {
        send_to_channel("DEBUG: prompt NULL");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        send_to_channel("DEBUG: curl init failed");
        return NULL;
    }

    char *response = malloc(1);
    response[0] = 0;

    char post_data[1024];

    snprintf(post_data, sizeof(post_data),
        "{\"model\":\"gpt-image-1\",\"prompt\":\"%s\",\"size\":\"1024x1024\"}",
        prompt);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,"Content-Type: application/json");
 headers = curl_slist_append(headers, "Authorization: Bearer sk-YOUR API key");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/images/generations");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        send_to_channel("DEBUG: OpenAI request failed");
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(response);
        return NULL;
    }

   // send_to_channel("DEBUG: OpenAI response received");

    /* chercher b64_json */

    char *b64_key = strstr(response,"\"b64_json\"");
    if (!b64_key) {
        send_to_channel("DEBUG: no b64_json found");
        send_to_channel(response);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(response);
        return NULL;
    }

    char *start = strchr(b64_key,'"');
    start = strchr(start+1,'"');
    start++;

    while (*start==' ' || *start==':') start++;

    if (*start=='"') start++;

    char *end = strchr(start,'"');

    if (!end) {
        send_to_channel("DEBUG: malformed b64_json");
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(response);
        return NULL;
    }

    size_t len = end-start;

    char *b64_data = malloc(len+1);
    strncpy(b64_data,start,len);
    b64_data[len]=0;

    free(response);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    /* écrire base64 dans fichier */

    char b64_file[]="/tmp/zozo_img.b64";
    FILE *f=fopen(b64_file,"w");

    if(!f){
        send_to_channel("DEBUG: failed to open b64 file");
        free(b64_data);
        return NULL;
    }

    fwrite(b64_data,1,len,f);
    fclose(f);

    free(b64_data);

    /* décoder en PNG */

    char png_file[]="/tmp/zozo_img.png";

    char cmd[512];
    snprintf(cmd,sizeof(cmd),"base64 -d %s > %s",b64_file,png_file);
    system(cmd);

    /* upload vers ImgBB */

    CURL *curl_imgbb=curl_easy_init();

    if(!curl_imgbb){
        send_to_channel("DEBUG: curl ImgBB failed");
        return NULL;
    }

    char *imgbb_response=malloc(1);
    imgbb_response[0]=0;

    curl_mime *mime=curl_mime_init(curl_imgbb);

    curl_mimepart *part=curl_mime_addpart(mime);
    curl_mime_name(part,"image");
    curl_mime_filedata(part,png_file);

    part=curl_mime_addpart(mime);
    curl_mime_name(part,"key");
    curl_mime_data(part,"1d4b24ab1f37339f9be9dc012cc67320",CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl_imgbb,CURLOPT_URL,"https://api.imgbb.com/1/upload");
    curl_easy_setopt(curl_imgbb,CURLOPT_MIMEPOST,mime);
    curl_easy_setopt(curl_imgbb,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl_imgbb,CURLOPT_WRITEDATA,&imgbb_response);

    res=curl_easy_perform(curl_imgbb);

    if(res!=CURLE_OK){
        send_to_channel("DEBUG: ImgBB upload failed");
        curl_easy_cleanup(curl_imgbb);
        curl_mime_free(mime);
        free(imgbb_response);
        return NULL;
    }

    curl_easy_cleanup(curl_imgbb);
    curl_mime_free(mime);

    /* récupérer URL */

    char *url_key=strstr(imgbb_response,"\"url\"");

    if(!url_key){
        send_to_channel("DEBUG: no url in imgbb response");
        free(imgbb_response);
        return NULL;
    }

    char *u=strchr(url_key,'"');
    u=strchr(u+1,'"');
    u++;

    while(*u==' '||*u==':') u++;

    if(*u=='"') u++;

    char *ue=strchr(u,'"');

    size_t ulen=ue-u;

char *raw_url = malloc(ulen+1);

strncpy(raw_url,u,ulen);
raw_url[ulen]=0;

char *final_url = remove_slashes(raw_url);

free(raw_url);
free(imgbb_response);

return final_url;
}

char *generate_image_tiny(const char *prompt)
{
    char *url = generate_image(prompt);
    if (!url) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) return url;

    char request[2048];
    snprintf(request,sizeof(request),
        "https://tinyurl.com/api-create.php?url=%s",url);

    char *resp = malloc(1);
    resp[0]=0;

    curl_easy_setopt(curl,CURLOPT_URL,request);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&resp);

    CURLcode res=curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    if(res!=CURLE_OK){
        free(resp);
        return url;
    }

    free(url);
    return resp;
}
