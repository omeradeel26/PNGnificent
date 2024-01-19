#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "paster.h"
#include "./starter/png_util/lab_png.h"
#include "./starter/png_util/crc.h"
#include "./starter/png_util/zutil.h"

//declaring global variables
char *images[50];
int num_images = 0;
const char *url_img[3][3] = {
  {"http://ece252-1.uwaterloo.ca:2520/image?img=1",
  "http://ece252-2.uwaterloo.ca:2520/image?img=1", 
  "http://ece252-3.uwaterloo.ca:2520/image?img=1"},
  {"http://ece252-1.uwaterloo.ca:2520/image?img=2",
   "http://ece252-2.uwaterloo.ca:2520/image?img=2",
   "http://ece252-3.uwaterloo.ca:2520/image?img=2"},
  {"http://ece252-1.uwaterloo.ca:2520/image?img=3",
  "http://ece252-2.uwaterloo.ca:2520/image?img=3",
  "http://ece252-3.uwaterloo.ca:2520/image?img=3"}
};

//get header inormation from png segement
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
  int realsize = size * nmemb;
  RECV_BUF *p = (RECV_BUF*)userdata;
  
  if (realsize > strlen(ECE252_HEADER) &&
  strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

  /* extract img sequence number */
  p->seq = atoi(p_recv + strlen(ECE252_HEADER));

  }
  return realsize;
}

//get png data using curl
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = (char*)realloc(p->buf, new_size);
        if (q == NULL) {
          perror("realloc"); /* out of memory */
          return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

//initialzied buffer for png data
int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
      return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	    return 2;
    }
    
    ptr->buf = (char*)p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}

//cleaning buffer --> freeing memory
int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	    return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

//retrieving images (png segments)
void *fetch_images(void *n_params){
  //getting url to fetch
  params* pars = (params*)n_params;
  char * url = pars->url;

  //repeat curl till all 50 images are fetched
  while (num_images < 50) {
    //creating curl handler
    CURL *curl_handle;
    CURLcode res;

    //initializing buffer for png data
    RECV_BUF recv_buf;
    recv_buf_init(&recv_buf, BUF_SIZE);
    
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
      fprintf(stderr, "curl_easy_init: returned NULL\n");
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    
    /* get it! */
    res = curl_easy_perform(curl_handle);
    
    //assigning buffer data to respective segment in global array
    if (images[recv_buf.seq] == NULL){
      images[recv_buf.seq] = recv_buf.buf;
      num_images+=1;
    } else {
      recv_buf_cleanup(&recv_buf);
    }

    /* cleaning up */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
  }

  return NULL;
}


//function to concatenate pngs
void catpng(){
    //setting width and height for final png
    const int width = 400;
    const int height = 300;

    //allocate memory for buffer containing PNG data
    U64 all_size = height*((width*4)+1);
    U8 *all_buf = (U8*)malloc(all_size * sizeof(U8));

    //size of uncompressed destination buffer
    int dest_size = height*((width*4)+1);

    //byte location of all_buf
    U64 buf_place = 0;
    
    for (int i = 0; i < 50; i++){

      if (images[i] == NULL){
        printf("image %i not working\n", i);
        continue;
      }
      //open file as binary 
      char *curr_img = images[i];

      //seek to start of IDAT chunk and getting length of IDAT
      U32 IDAT_len;

      //storing data length
      memcpy(&IDAT_len, curr_img+33, 4);
      IDAT_len = ntohl(IDAT_len);

      //copy file content to buffer, allocating memory
      U8 *IDAT_source_buf = (U8*)malloc(IDAT_len*sizeof(U8));
      U8 *IDAT_dest_buf = (U8*)malloc(dest_size*sizeof(U8));
      
      //copy IDATA to buffer
      memcpy(IDAT_source_buf, curr_img+41, IDAT_len);

      //decompress IDAT data
      U64 dest_len;
      mem_inf(IDAT_dest_buf, &dest_len, IDAT_source_buf, IDAT_len);

      //copy data from uncompressed buffer to large buffer containing png data
      memcpy(all_buf+buf_place, IDAT_dest_buf, dest_len);
      
      //increment pointer in large buffer containing PNG data
      buf_place += dest_len; 

      //remove memory leaks
      free(IDAT_source_buf);
      free(IDAT_dest_buf);
    }

    //allocate memory for compressed PNG data
    U8 *final_buf = (U8*)malloc(all_size * sizeof(U8));
    U64 final_len;

    //Compress large data buffer and assign to final IDAT buffer
    mem_def(final_buf, &final_len, all_buf, buf_place, Z_DEFAULT_COMPRESSION);
    free(all_buf);

    //creating and opening all.png file
    FILE *fp = fopen("./all.png", "wb+");

    //write header of file
    U8 signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(signature, 1, 8, fp);

    //Write IHDR Chunk info to file

    //IHDR length and type
    U8 final_IHDR_len[]= {0x00, 0x00, 0x00, 0x0D};
    U8 final_IHDR_type[] = {0x49, 0x48, 0x44, 0x52};

    //copy width to byte array
    U32 final_width = htonl(width);

    //copy height to byte array
    U32 final_height = htonl(height);

    //the rest of IHDR data ie. depth  etc.
    U8 final_IHDR_data[] = {0x08, 0x06, 0x00, 0x00, 0x00};

    //copy information for calculating crc buffer
    unsigned char IHDR_crc_buf[17];
    memcpy(IHDR_crc_buf, final_IHDR_type, 4);
    memcpy(&IHDR_crc_buf[4], &final_width, 4);
    memcpy(&IHDR_crc_buf[8], &final_height, 4);
    memcpy(&IHDR_crc_buf[12], final_IHDR_data, 5);

    //calculate crc of IHDR chunk
    long IHDR_crc_calculated = crc(IHDR_crc_buf, 17);
    IHDR_crc_calculated = htonl(IHDR_crc_calculated);

    //write IHDR chunk to file
    fwrite(final_IHDR_len, 1, 4, fp);
    fwrite(final_IHDR_type, 1, 4, fp);
    fwrite(&final_width, 4, 1, fp);
    fwrite(&final_height, 4, 1, fp);
    fwrite(final_IHDR_data, 1, 5, fp);
    fwrite(&IHDR_crc_calculated, 4,1, fp);

    //Write IDAT chunk information
    U32 new_final_len = htonl(final_len);

    //ASCII type for IDAT
    U8 final_IDAT_type[] = {0x49, 0x44, 0x41, 0x54};

    //create buffer for crc
    unsigned char *IDAT_crc_buf = (unsigned char*)malloc((4+final_len) * sizeof(unsigned char));

    //copy type and data buffer to crc buffer
    memcpy(IDAT_crc_buf, final_IDAT_type, 4);
    memcpy(&IDAT_crc_buf[4], final_buf, final_len);

    //calculate crc of IDAT chunk
    long IDAT_crc_calculated = crc(IDAT_crc_buf, 4+final_len);
    IDAT_crc_calculated = htonl(IDAT_crc_calculated);
    free(IDAT_crc_buf);

    //write IDAT chunk to file
    fwrite(&new_final_len, 4, 1, fp);
    fwrite(final_IDAT_type, 1, 4, fp);
    fwrite(final_buf, 1, final_len, fp);
    fwrite(&IDAT_crc_calculated, 4, 1, fp);

    //Write IEND chunk information
    U8 final_IEND_len[] = {0x00, 0x00, 0x00, 0x00};
    unsigned char final_IEND_type[] = {0x49, 0x45, 0x4E, 0x44};
     
    //calculate CRC of IEND chunk
    long IEND_crc_calculated = crc(final_IEND_type, 4);
    IEND_crc_calculated = htonl(IEND_crc_calculated);

    //write IEND chunk to file
    fwrite(final_IEND_len, 1, 4, fp);
    fwrite(final_IEND_type, 1,4, fp);
    fwrite(&IEND_crc_calculated, 4, 1, fp);

    //closing file
    fclose(fp);

    //remove memory leaks
    free(final_buf);
}

int main(int argc, char **argv)
{
  //parsing user input
  int c;
  int t = 1;
  int n = 1;
  const char *str = "option requires an argument";
  
  while ((c = getopt (argc, argv, "t:n:")) != -1) {
      switch (c) {
      case 't':
    t = strtoul(optarg, NULL, 10);
    if (t <= 0) {
              fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
              return -1;
          }
          break;
      case 'n':
          n = strtoul(optarg, NULL, 10);
          if (n <= 0 || n > 3) {
              fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
              return -1;
          }
          break;
      default:
          return -1;
      }
  }

  //initializing global array with nullptrs to start with
  for (int i = 0; i < 50; i++){
    images[i] = NULL;
  }

  //creating array of threads
  pthread_t *p_tids;
  p_tids = (pthread_t*)malloc(sizeof(pthread_t) * t);

  //declaring round-robin index
  int server_index = 0;
  for (int i = 0; i < t; i++){
    //reset index if it goes out of bound
    if(server_index == 3){
      server_index = 0;
    }

    //set up parameters for fetching
    params url_params;
    url_params.url = (char*)url_img[n-1][server_index];

    //creating thread to fetch images
    pthread_create(&p_tids[i], NULL, fetch_images, &url_params);
    server_index++;
  }

  //joining threads together
  for (int i = 0; i < t; i++){
    pthread_join(p_tids[i], NULL);
  }
  
  //concatenating retreived image segments together
  catpng();

  //remove memory leaks
  for (int i = 0; i < 50; i++){
    free(images[i]);
    images[i] = NULL;
  }
  free(p_tids);

  //exit status
  return 0;
}
