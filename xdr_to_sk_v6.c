// use this in Text Editor Ctl+F12
/*
#!/bin/sh
gcc -c -g xdr_to_sk_v6.c
gcc -o xdr_to_sk xdr_to_sk_v6.o tinyexpr.o -lm -lpthread -lconfig
chmod +x xdr_to_sk
*/

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libconfig.h>
#include "tinyexpr.h"

#define MINMEA_MAX_LENGTH 150
#define CBSIZE 1024
#define PORT 10250
#define S_LABEL "XDR_Parser"
#define S_TYPE "NMEA0183"
#define ATT_OBJECT "Vessel attitude"

float last_roll = 0.0;
float last_pitch = 0.0;
float last_yaw = 0.0;

typedef struct cbuf {
    char buf[CBSIZE];
    int fd;
    unsigned int rpos, wpos;
} cbuf_t;

struct XDR_quad
{
	char type[20];	 // from config file
	char data[20];
	char units[20];
	char name[20];
	char expression[20];
	char signalk_path[50];
};

struct src
{
        char label[11]; // always = "XDR_Parser"
        char type[9];  // always = "NMEA0183"
        char talker[3]; // e.g. II, VI, VV etc
        char sentence[4]; // should alkways be "XDR" but written from message just in case
};

struct comport
{
	const char *device;
	const char *parity;
	int baudrate;
	int databits;
	int stopbits;
};

struct tcp_port
{
	const char *address;
	int port;
};

int fill_quad_arr(const char *str, struct XDR_quad xdr_arr[]);
bool do_checksum(const char *sentence, bool strict);
uint8_t get_checksum(const char *sentence);
static int hex2int(char c);
bool is_XDR(const char *sentence);
void fill_src(const char *str, const char *hdr, struct src *source);
void strip_header_checksum(char * str);
int get_sensor_match(const config_t cfg , struct XDR_quad *this_quad);
int get_num_quads(const char str[]);

bool convert_data(struct XDR_quad *xdr);
void createvalstr(struct XDR_quad xdr_arr[], const int itr, char *values_str);
bool is_tcp_in_defined(const config_t cfg , struct tcp_port *port);
bool is_tcp_out_defined(const config_t cfg , struct tcp_port *port);
bool open_tcp_client(cbuf_t *cbuf, struct tcp_port *port);
bool open_tcp_server(int server_fd, struct tcp_port *port, int *n_socket);

bool write_sk(char *in_str, const config_t cfg, char *out_str);
int read_line(cbuf_t *cbuf, char *dst, unsigned int size);

int main()
{
	last_yaw = last_pitch = last_roll =0.0;
	config_t cfg;
	config_setting_t *setting;
	config_init(&cfg);
	if(! config_read_file(&cfg, "XDR_Dictionary.cfg"))
	{
	  fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
          	config_error_line(&cfg), config_error_text(&cfg));
	  config_destroy(&cfg);
	  return(EXIT_FAILURE);
  	}

	struct tcp_port * tcp_parms_in;
	tcp_parms_in = (struct tcp_port *) malloc(sizeof(struct tcp_port));
	bool tcp_in_open = false;
	bool tcp_in_defined = is_tcp_in_defined(cfg, tcp_parms_in);
	cbuf_t *cbuf;
	cbuf = (cbuf_t *) calloc(1, sizeof(*cbuf));

	struct tcp_port * tcp_parms_out;
	tcp_parms_out = (struct tcp_port *) malloc(sizeof(struct tcp_port));
	bool tcp_out_open = false;
	bool tcp_out_defined = is_tcp_out_defined(cfg, tcp_parms_out);

	int server_fd;
	int *server_socket;
	server_socket = (int *) malloc(sizeof(int));

	if (tcp_out_defined)
		tcp_out_open = open_tcp_server(server_fd, tcp_parms_out, server_socket);

	if (tcp_in_defined)
	{
		tcp_in_open = open_tcp_client(cbuf, tcp_parms_in);
		char *str;
		str = (char *) malloc(CBSIZE);
		char *sk_str;
		sk_str = (char *) malloc(CBSIZE);

		if (tcp_in_open)
		{
			int i = 1;
			while (i > 0)
			{
				int j = read_line(cbuf, str, CBSIZE);
				char * newstr;
				newstr = (char *) malloc(CBSIZE);
				memmove(newstr, str, CBSIZE);
				str[0] = 0;
				if (write_sk(newstr, cfg, sk_str))
					if (tcp_out_open)
					{
//						printf("%s", sk_str);
						write(*server_socket , sk_str , strlen(sk_str)  );
					}
					else
						printf("%s\n", sk_str);
				else
					printf("Not an XDR message.\n");
				free(newstr);
			}
		}
		free(str); free(sk_str);
       	}
	else
	{
		printf("TCP in not configured for input. Program expects NMEA XDR messages as input via stdin\n");
		char str[CBSIZE];
		char sk_str[CBSIZE];
		while (fgets(str, sizeof(str), stdin) != 0)
			{
				char newstr[CBSIZE];
				memmove(newstr, str, CBSIZE);
				str[0] = 0;
				if (write_sk(newstr, cfg, sk_str))
					if (tcp_out_open)
						write(*server_socket , sk_str , strlen(sk_str) );
					else
						printf("%s\n", sk_str);
				else
					printf("Not an XDR message.\n");
			}
        }


	if(tcp_in_open)
		close(cbuf->fd);
	if (tcp_out_open)
		close(server_fd);
	free(tcp_parms_in);
	free(cbuf);
	config_destroy(&cfg);
	return 0;
}
//----------------------------------------------End of Main--------------------------------------------------------------------

int read_line(cbuf_t *cbuf, char *dst, unsigned int size)
{
    unsigned int i = 0;
    ssize_t n;

    while (i < size)
    {
       if (cbuf->rpos == cbuf->wpos)
	{
            size_t wpos = cbuf->wpos % CBSIZE;
	    //if ((n = read(cbuf->fd, cbuf->buf + wpos, (CBSIZE - wpos))) < 0)
	    if( (n = recv(cbuf->fd, cbuf->buf + wpos, (CBSIZE - wpos), 0)) < 0)
	    {
		if (errno == EBADF)
		    printf("socket is not a valid socket descriptor.\n");
		if (errno == ECONNRESET)
		    printf("A connection was forcibly closed by a peer.\n");
		if (errno == EFAULT)
		    printf("Using the buf and len parameters would result in an attempt to access storage outside the caller's "\
                           "address space.\n");
                if (errno == EINTR)
		     printf("The recv() call was interrupted by a signal that was caught before any data was available.\n");
		if (errno == EINVAL)
		    printf("The request is invalid or not supported. The MSG_OOB flag is set and no out-of-band data is available.\n");
		if (errno == EIO)
		    printf("There has been a network or transport failure.\n");
		if (errno == ENOBUFS)
		    printf("Insufficient system resources are available to complete the call.\n");
		if (errno == ENOTCONN)
		    printf("A receive is attempted on a connection-oriented socket that is not connected.\n");
		if (errno == ENOTSOCK)
		    printf("The descriptor is for a file, not for a socket.\n");
		if (errno == EOPNOTSUPP)
		    printf("The specified flags are not supported for this socket type or protocol.\n");
		if (errno == ETIMEDOUT)
		    printf("The connection timed out during connection establishment, or due to a transmission timeout on active "\
                           "connection.\n");
		if (errno == EWOULDBLOCK)
		    printf("socket is in nonblocking mode and data is not available to read. Or the SO_RCVTIMEO timeout value was "\
			   "reached before data was available.\n");
                return -1;
            } else if (n == 0)
                return 0;
            cbuf->wpos += n;
        }
        dst[i++] = cbuf->buf[cbuf->rpos++ % CBSIZE];
        if (dst[i - 1] == '\n') break;
    }
    if(i == size)
    {
	fprintf(stderr, "line too long.");
	return 0;
    }
    dst[i] = 0;
    return i;
}

bool convert_data(struct XDR_quad *xdr)
  // expects an expression from dictionary file in the form of e.g. (x+273) or ((x*9/5)+32)
  // expects the quad to have the units filed filled from the dictionary
{
	char * c;
	c = (char *) malloc(32);
	char * xpos = strchr(xdr->expression, 'x');
	if (xpos == NULL)
		return false;
	else
	{
		char * startpos = xdr->expression;
		int i = 0;
		while (startpos != xpos) // write contents of expression to c up to the 'x' character
		{
			*(c + i) = *startpos;
			i++;
			startpos++;
		}
		char * datapos = xdr->data;
		while (*datapos != '\0') // write the contents of the data field into c
		{
			*(c + i) = *datapos;
			i++;
			datapos++;
		}
		startpos++;
		while (*startpos != '\0')
		{
			*(c+i) = *startpos;
			i++;
			startpos++;
		}
		*(c+i) = '\0';
	}

	if (strcmp(xdr->units, "int") == 0)
	{
		int r = te_interp(c, 0);
		snprintf(xdr->data, sizeof(xdr->data), "%d",r);
	}

	if (strcmp(xdr->units, "float") == 0)
	{
		float r = te_interp(c, 0);
		snprintf(xdr->data, sizeof(xdr->data), "%0.2f",r);
	}

	if (strcmp(xdr->units, "radians") == 0)
	{
		float r = te_interp(c, 0);
		snprintf(xdr->data, sizeof(xdr->data), "%0.6f",r); // radians need more precision
	}
	free(c);
	return true;
}

void createvalstr(struct XDR_quad xdr_arr[], const int itr, char *values_str)
{
	convert_data(&xdr_arr[itr]);

	if  (strcmp(xdr_arr[itr].type, ATT_OBJECT) ==0)
	{
		if (strcmp(xdr_arr[itr].name, "ROLL") == 0)
		{
			last_roll = atof(xdr_arr[itr].data);
			sprintf(values_str, "{\"path\":\"%s\",\"value\":{\"roll\":%0.6f,\"pitch\":%0.6f,\"yaw\":%0.6f}},",
				xdr_arr[itr].signalk_path, last_roll, last_pitch, last_yaw);
		}

		if (strcmp(xdr_arr[itr].name, "PTCH") == 0)
		{
			last_pitch = atof(xdr_arr[itr].data);
			sprintf(values_str, "{\"path\":\"%s\",\"value\":{\"roll\":%0.6f,\"pitch\":%0.6f,\"yaw\":%0.6f}},",
				xdr_arr[itr].signalk_path, last_roll, last_pitch, last_yaw);
		}

		if (strcmp(xdr_arr[itr].name, "YAW") == 0)
		{
			last_yaw = atof(xdr_arr[itr].data);
			sprintf(values_str, "{\"path\":\"%s\",\"value\":{\"roll\":%0.6f,\"pitch\":%0.6f,\"yaw\":%0.6f}},",
				xdr_arr[itr].signalk_path, last_roll, last_pitch, last_yaw);
		}
	}
	else
		sprintf(values_str,"{\"path\":\"%s\",\"value\":%s},", xdr_arr[itr].signalk_path, xdr_arr[itr].data);
}

int get_num_quads(const char str[])
{  // assumes that the NMEA message has been stripped of header & checksum
  int word_cnt = 1;
  int quad_ctr = 0;
  char *p;
  p = strchr (str,',');
  while (p!=NULL)
  {
    p=strchr(p+1,',');
    word_cnt++;
  }
  return word_cnt/4;
}

int fill_quad_arr(const char *str, struct XDR_quad xdr_arr[])
{
  char *p;
  p = strchr (str,',');
  int word_cnt = 1;
  int quad_ctr = 0;
  int quad_element = 0;
  int prevpos = 0;
  while (p!=NULL)
  {
	char word[50];
	int nextpos = p-str+1;
	size_t sizeword = nextpos-prevpos-1;
	memmove(word, &(str[prevpos]), sizeword);
	word[sizeword]='\0';

	quad_element = (int) (word_cnt-1)%4;
	quad_ctr = (word_cnt-1)/4;
	if (quad_element == 0)
	    strcpy(xdr_arr[quad_ctr].type,  word);
	if (quad_element == 1)
	    strcpy(xdr_arr[quad_ctr].data,  word);
	if (quad_element == 2)
	    strcpy(xdr_arr[quad_ctr].units, word);
	if (quad_element == 3)
	    strcpy(xdr_arr[quad_ctr].name,  word);

	p=strchr(p+1,',');
	prevpos = nextpos;
	word_cnt++;

	if (p==NULL)
	{
	  prevpos=nextpos;
	  nextpos=strlen(str)+1;
	  sizeword = nextpos-prevpos-1;
	  memmove(&word, &(str[prevpos]), sizeword);
	  word[sizeword]='\0';

	  quad_element = (int) (word_cnt-1)%4;
	  quad_ctr = (word_cnt-1)/4;
	  if (quad_element == 0)
		  strcpy(xdr_arr[quad_ctr].type,  word);
	  if (quad_element == 1)
		  strcpy(xdr_arr[quad_ctr].data,  word);
	  if (quad_element == 2)
		  strcpy(xdr_arr[quad_ctr].units, word);
	  if (quad_element == 3)
		  strcpy(xdr_arr[quad_ctr].name,  word);
	}
  }
  return 0;
}


static int hex2int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

uint8_t get_checksum(const char *sentence)
{
    // Support senteces with or without the starting dollar sign.
    if (*sentence == '$')
        sentence++;

    uint8_t checksum = 0x00;

    // The optional checksum is an XOR of all bytes between "$" and "*".
    while (*sentence && *sentence != '*')
        checksum ^= *sentence++;

    return checksum;
}

bool do_checksum(const char *sentence, bool strict)
{
    uint8_t checksum = 0x00;

    // Sequence length is limited.
    if (strlen(sentence) > MINMEA_MAX_LENGTH + 3)
    {
        printf("Length failed.\n");
	return false;
    }
    // A valid sentence starts with "$".
    if (*sentence++ != '$')
    {
	printf("Missing '$'. \n");
	return false;
    }
    // The optional checksum is an XOR of all bytes between "$" and "*".
    while (*sentence && *sentence != '*' && isprint((unsigned char) *sentence))
        checksum ^= *sentence++;

    // If checksum is present...
    if (*sentence == '*') {
        // Extract checksum.
        sentence++;
        int upper = hex2int(*sentence++);
        if (upper == -1)
        {
            printf("Missing data.\n");
	    return false;
	}
        int lower = hex2int(*sentence++);
        if (lower == -1)
        {
	    printf("Missing data.\n");
    	    return false;
	}
        int expected = upper << 4 | lower;

        // Check for checksum mismatch.
        if (checksum != expected)
        {
	    printf("Checksum msmatch.\n");
	    return false;
	}
    }
    else if (strict) {
        // Discard non-checksummed frames in strict mode.
        return false;
    }

    // The only stuff allowed at this point is a newline.
    if (*sentence && strcmp(sentence, "\n") && strcmp(sentence, "\r\n"))
        return false;

    return true;
}

bool is_XDR(const char *sentence)
{   // the function assumes that the sentence has passed the checks in checksum
    char header[4];
    memmove(header, (sentence+3), 3);
    header[3] = '\0';

    if (strcmp(header, "XDR") == 0)
	return true;
    else
      return false;
}

void fill_src(const char *str, const char *hdr, struct src *source)
{    // the function assumes that the sentence has passed the checks in checksum
    strcpy(source->label, S_LABEL);
    strcpy(source->type,  S_TYPE);

    char talkerid[3];
    memmove(talkerid, (str+1), 2);
    talkerid[2] = '\0';
    strcpy(source->talker, talkerid);
    strcpy(source->sentence, hdr);
}

void strip_header_checksum(char * str)
{
    int oldlen = strlen(str);
    int frontparse = strlen("$xxXDR");
    char * backparse = strchr(str, '*');
    if (backparse != NULL) str[backparse-str] = '\0';
    memmove(str, (&str[7]), strlen(str));
}

bool is_tcp_in_defined(const config_t cfg , struct tcp_port *port)
{
	// Get the TCP address and port 
  	if(config_lookup_string(&cfg, "TCP_in_address", &port->address))
  	{
  		if(config_lookup_int(&cfg, "TCP_in_port", &port->port))
			return true;
  		else
			return false;
  	}
  	else
		return false;
}

bool is_tcp_out_defined(const config_t cfg , struct tcp_port *port)
{
	// Get the TCP address and port
  	if(config_lookup_string(&cfg, "TCP_out_address", &port->address))
  	{
  		if(config_lookup_int(&cfg, "TCP_out_port", &port->port))
			return true;
  		else
			return false;
  	}
  	else
		return false;
}

bool open_tcp_client(cbuf_t *cbuf, struct tcp_port *port)
{
	struct sockaddr_in saddr;
	struct hostent *h;
	char *ip;

	if(!(h = gethostbyname(port->address)))
	{
		perror("client: gethostbyname failed");
		exit(EXIT_FAILURE);
	}
	ip = inet_ntoa(*(struct in_addr*)h->h_addr);

	if((cbuf->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("client: socket create failed");
		exit(EXIT_FAILURE);
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port->port);
	inet_aton(ip, &saddr.sin_addr);
	if(connect(cbuf->fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
	{
		perror("client: connect to server failed");
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Input connected to ip: %s port: %d\n", ip, port->port);
	return true;
}

bool open_tcp_server(int server_fd, struct tcp_port *port, int *n_socket)
{
	struct sockaddr_in saddr;
	struct hostent *h;
	int addrlen = sizeof(saddr);
	char *ip;

	if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("server: socket create failed");
		exit(EXIT_FAILURE);
	}

	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
	{
	      	perror("server: set sock opt failed");
		exit(EXIT_FAILURE);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(port->port);
//	ip = inet_ntoa(*(struct in_addr*)port->address);
//	inet_aton(ip, &saddr.sin_addr);
	if ( bind(server_fd, (struct sockaddr *)&saddr, sizeof(saddr)) <0 )
	{
		perror("server: bind failed");
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 3) < 0)
	{
		perror("server: listen failed");
		exit(EXIT_FAILURE);
	}
	else
		printf("Server listening.\n");

	if (( *n_socket = accept(server_fd, (struct sockaddr *)&saddr, (socklen_t*)&addrlen))<0)
	{
		perror("server: incoming connection accept failed");
		exit(EXIT_FAILURE);
	}
	else
		printf("Output connected to port: %d\n", port->port);
 	return true;
}


int get_sensor_match(const config_t cfg , struct XDR_quad *this_quad)
{
	const char *name, *path, *expression, *units, *type;
	config_setting_t * sensor_list = config_lookup(&cfg, "XDR_Dictionary.sensors");
 	int count = config_setting_length(sensor_list);

 	for(int i = 0; i < count; ++i)
 	{  // iterate through the sensor list - for each sensor lookup the name
 		config_setting_t *this_sensor = config_setting_get_elem(sensor_list, i);
       		if (config_setting_lookup_string(this_sensor, "name", &name) == CONFIG_TRUE);
		if (strcmp(name, this_quad->name) == 0) //does this sensor name match our XDR name?
		{  //Yes, there is a match so set the corresponding path, converter and units values in the XDR quad
			if (config_setting_lookup_string(this_sensor, "sk_path", &path) == CONFIG_TRUE)
			{
				strcpy(this_quad->signalk_path, path);
				if (config_setting_lookup_string(this_sensor, "expression", &expression) == CONFIG_TRUE)
					strcpy(this_quad->expression, expression);
				if (config_setting_lookup_string(this_sensor, "units", &units) == CONFIG_TRUE)
					strcpy(this_quad->units, units);
				if (config_setting_lookup_string(this_sensor, "type", &type) == CONFIG_TRUE)
					strcpy(this_quad->type, type);
				return 0; // match found, so stop looking
			}
		} // no match iterate to next sensor
	} // no match in config file
	char tmpstr[100];
	strcpy(tmpstr, this_quad->name);
	strcat(tmpstr, " is not defined in config file.\0");
	int len2 = strlen(tmpstr);
	strcpy(this_quad->signalk_path, tmpstr);
	return -1;
}

bool write_sk(char * in_str, const config_t cfg, char * out_str)
{
	if (do_checksum(in_str, true) )
	{
	  if (is_XDR(in_str)) // process it.
	  {
		struct src source;
		fill_src(in_str, "XDR", &source);
		strip_header_checksum(in_str);
		int num_quads = get_num_quads(in_str);
		struct XDR_quad xdr_arr[num_quads];
		fill_quad_arr(in_str, xdr_arr);

		char valuestr[500];
		strcpy(valuestr, "\"values\":[");

		for (int itr =0; itr < num_quads; itr++)
		{
			if (get_sensor_match(cfg , &xdr_arr[itr]) == 0) // then add data to the output json string
			{
				char tempstr[500];
				createvalstr(xdr_arr, itr, tempstr);
				strcat(valuestr, tempstr);
			} // if no match then dont add
		}
		valuestr[strlen(valuestr)-1] = ']'; // replace the last comma with square bracket

		const char * mmsi;
		mmsi =  (const char *) malloc(10);
		config_lookup_string(&cfg, "mmsi", &mmsi);

//		char mmsi_str[10];
//		strcpy(mmsi_str, mmsi);
//		free(mmsi);

		char sourcestr[200];
		sprintf(sourcestr,"\"source\":{\"label\":\"%s\",\"type\":\"%s\",\"talker\":\"%s\",\"sentence\":\"%s\"},",
				source.label, source.type, source.talker, source.sentence);

		sprintf(out_str, "{\"context\":\"vessels.urn:mrn:imo:mmsi:%s\",\"updates\":[{%s%s}]}\n\0",
				mmsi, sourcestr, valuestr);
	  }
	  else
	  {
		printf("Not 'XDR' : %s\n", in_str);
		in_str[0] = '\0';
		return false;
	  }
	}
	else
	{
		printf("Not 'NMEA' : %s\n", in_str);
		in_str[0] = '\0';
		return false;
	}
	return true;
}
