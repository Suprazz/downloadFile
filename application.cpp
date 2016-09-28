#include "Particle.h"
#include "spiffs.h"
#include "Adafruit_TinyFlash.h"


//SYSTEM_MODE(SEMI_AUTOMATIC);

int RequestFile(const char * url, const char * urlPath, const char * destinationFilename);
void ReceivingFile();



static spiffs fs;
#define LOG_PAGE_SIZE       256

static u8_t spiffs_work_buf[LOG_PAGE_SIZE * 2];
static u8_t spiffs_fds[32 * 4];
static u8_t spiffs_cache_buf[(LOG_PAGE_SIZE + 32) * 4];
Adafruit_TinyFlash flash;

static void test_spiffs();
static int32_t my_spiffs_read(uint32_t addr, uint32_t size, uint8_t *dst) {
	flash.readData(dst, addr, size);
	return SPIFFS_OK;
}

static int32_t my_spiffs_write(uint32_t addr, uint32_t size, uint8_t *src) {
	if (flash.writePage(addr, src, size))
		return SPIFFS_OK;
	return SSPIFFS_WRITE_ERR;
}

static int32_t my_spiffs_erase(uint32_t addr, uint32_t size) {
	if (flash.eraseSector(addr))
		return SPIFFS_OK;
	return SPIFFS_ERASE_ERR;
}


void my_spiffs_mount() {
	spiffs_config cfg;
	// 	cfg.phys_size = 2 * 1024 * 1024; // use all spi flash
	// 	cfg.phys_addr = 0; // start spiffs at start of spi flash
	// 	cfg.phys_erase_block = 65536; // according to datasheet
	// 	cfg.log_block_size = 65536; // let us not complicate things
	// 	cfg.log_page_size = LOG_PAGE_SIZE; // as we said

	cfg.hal_read_f = my_spiffs_read;
	cfg.hal_write_f = my_spiffs_write;
	cfg.hal_erase_f = my_spiffs_erase;

	int res = SPIFFS_mount(&fs,
		&cfg,
		spiffs_work_buf,
		spiffs_fds,
		sizeof(spiffs_fds),
		spiffs_cache_buf,
		sizeof(spiffs_cache_buf),
		0);
	//Serial.printf("mount res: %i\n", res);
}

TCPClient client;
char server[] = "vectorlogo.biz";
char path[] = "/wp-content/uploads/2013/01/GOOGLE-VECTORLOGO-BIZ-128x128.png";



void setup()
{
	Serial.begin(115200);
	while (!Serial.available())
	{
		Particle.process();
	}
	Serial.println("Hello SPIFFS");

	if (flash.begin(A7))
	{
		// Flash is compatible
	}
	else	
	{
		// Incorrect model number or unable to communicate with flash
		Serial.println("Flash init error");
		while (1);
	}
	flash.WriteStatusRegister(0);
	
	// Erase all flash. Do this only the first time you use the flash chip!
	//flash.eraseChip();
	
	my_spiffs_mount();

	//test_spiffs();

	Serial.println("connecting...");
	RequestFile(server, path, "tcpFile");
	
}


void loop()
{
	ReceivingFile();

}

typedef enum
{
	ReceptionState_Idle = 0,
	ReceptionState_ScanHeader,
	ReceptionState_ScanContentLength,
	ReceptionState_readData,

}ReceptionState;

//#define DEBUG_RECEIVED_DATA(...) Serial.printlnf(__VA_ARGS__)
#define DEBUG_RECEIVED_DATA(...)

spiffs_file destinationFile;
ReceptionState receiveState = ReceptionState_Idle;
const char * dstFile;

// Return 1 if success
int RequestFile(const char * url, const char * urlPath, const char * destinationFilename)
{
	String s;
	int success = 0;

	client.flush();
	client.stop();

	if (client.connect(url, 80))
	{
		Serial.println("connected");
		String urlp = String(urlPath);
		String urls = String(url);

		s = "GET " + urlp + " HTTP/1.1\r\n" + "Host: " + urls + "\r\n\r\n";
		DEBUG_RECEIVED_DATA(s.c_str());
		client.print(s.c_str());
		dstFile = destinationFilename;
		receiveState = ReceptionState_ScanHeader;
		return 1;
	}
	else
	{
		Serial.println("connection failed");
	}


	return 0;
}



void ReceivingFile()
{
	static uint8_t tcpBuffer[512];
	static int readLength;
	int HTTPAnswerNumber;
	static int fileLength;
	static char* index;
	int byteWritten;
	uint8_t contentLength[] = "Content-Length:";

	if (client.available())
	{
		switch (receiveState)
		{
			case 1: // Decode header
					//		HTTP / 1.1 200 OK
					// 		Content-Type: text/xml; charset=utf-8
					// 		Content-Length: length
				if (client.available() > 50)
				{
					readLength = client.read(tcpBuffer, sizeof(tcpBuffer));
					DEBUG_RECEIVED_DATA("%d bytes received", readLength);
					DEBUG_RECEIVED_DATA((const char *)tcpBuffer);
					sscanf((const char *)&tcpBuffer[9], "%d", &HTTPAnswerNumber);

					if (HTTPAnswerNumber == 200)
					{
						// Server answer us a success, Continue
						receiveState = ReceptionState_ScanContentLength;
					}
					else
					{
						DEBUG_RECEIVED_DATA("Http Error: %d", HTTPAnswerNumber);
						client.flush();
						client.stop();
					}
				}
				else
				{
					// Not enough data
				}
				break;
			case ReceptionState_ScanContentLength:
				if (readLength < sizeof(tcpBuffer))
				{
					// If there is data available, we can add mode
					readLength += client.read(&tcpBuffer[readLength], sizeof(tcpBuffer)- readLength);
				}

				index = strstr((const char *)tcpBuffer, (const char *)contentLength);
				if (index != NULL)
				{
					// Content found , we know the length of the file, then read the file
					sscanf((const char *)(index + sizeof(contentLength)), "%ld", &fileLength);
					DEBUG_RECEIVED_DATA("File length is: %d", fileLength);
					// locate end of header with \r\n\r\n
					index = strstr((const char *)tcpBuffer, "\r\n\r\n");
					if (index != NULL)
					{
						index += 4;
						// Ok we can start to read and store the file
						int toWrite = readLength - ((index) - (char *)tcpBuffer);
						DEBUG_RECEIVED_DATA("Filename: %s", dstFile);
						destinationFile = SPIFFS_open(&fs, dstFile, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);
						DEBUG_RECEIVED_DATA("Filecontent:");
						DEBUG_RECEIVED_DATA(index);
						byteWritten = SPIFFS_write(&fs, destinationFile, index, toWrite);
						if (byteWritten == toWrite)
						{
							fileLength -= byteWritten;
							receiveState = ReceptionState_readData;
						}
						else
						{
							DEBUG_RECEIVED_DATA("errno %i\n", SPIFFS_errno(&fs));
							client.flush();
							client.stop();
						}
					}
					else
					{
						DEBUG_RECEIVED_DATA("End of header not found");
						client.flush();
						client.stop();
					}
				}
				else
				{
					// Content not found yet. We'll dispose the datalength minus the string length.
					strcpy((char *)tcpBuffer, (const char *)&tcpBuffer[readLength - sizeof(contentLength)]);
					readLength = sizeof(contentLength);

// 					DEBUG_RECEIVED_DATA("Content Error or not found");
// 					client.flush();
// 					client.stop();
				}


				break;

			case ReceptionState_readData:
				memset(tcpBuffer, 0, sizeof(tcpBuffer));
				readLength = client.read(tcpBuffer, sizeof(tcpBuffer));
				DEBUG_RECEIVED_DATA((const char *)tcpBuffer);

				byteWritten = SPIFFS_write(&fs, destinationFile, tcpBuffer, readLength);
				if (byteWritten == readLength)
				{
					fileLength -= byteWritten;
				}
				else
				{
					DEBUG_RECEIVED_DATA("Write errno %i\n", SPIFFS_errno(&fs));
					client.flush();
					client.stop();
				}

				if (fileLength == 0)
				{
					// File transfer ended.
					// close file
					//receiveState = ReceptionState_Idle;
					SPIFFS_close(&fs, destinationFile);
					DEBUG_RECEIVED_DATA("Transfer completed!");
					client.flush();
					client.stop();
				}


				break;
			default:
				break;
		}

	}

	if (!client.connected() && receiveState)
	{
		spiffs_file ff;
		Serial.println("Client disconnected.");
		receiveState = ReceptionState_Idle;
		client.stop();

		char buffer[256] = "";
		spiffs_stat s;
		

		// Print file output.
		Serial.printlnf("File output: %s", dstFile);
		
		ff = SPIFFS_open(&fs, dstFile, SPIFFS_RDONLY, 0);
		SPIFFS_fstat(&fs, ff, &s);
		int readLen = 0, totalRead =0;
		do 
		{
			readLen = SPIFFS_read(&fs, ff, (u8_t *)buffer, (totalRead + sizeof(buffer) < s.sizet) ? sizeof(buffer) : s.sizet - totalRead);
			totalRead += readLen;
			Serial.write((const uint8_t *)buffer, readLen);
		} while (readLen > 0 && totalRead < s.sizet);

		SPIFFS_close(&fs, ff);
		Serial.println("End of file");
	}


}



static void test_spiffs() {
	char buf[12];

	// Surely, I've mounted spiffs before entering here

	spiffs_file fd = SPIFFS_open(&fs, "my_file", SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);
	if (SPIFFS_write(&fs, fd, (u8_t *)"Hello world", 12) < 0) 
		Serial.printf("errno %i\n", SPIFFS_errno(&fs));
	SPIFFS_close(&fs, fd);

	fd = SPIFFS_open(&fs, "my_file", SPIFFS_RDWR, 0);
	if (SPIFFS_read(&fs, fd, (u8_t *)buf, 12) < 0) 
		Serial.printf("errno %i\n", SPIFFS_errno(&fs));
	SPIFFS_close(&fs, fd);

	Serial.printf("--> %s <--\n", buf);
	uint32_t total, used;
	SPIFFS_info(&fs, &total, &used);
	Serial.printf("Total: %d, Used: %d", total, used);
}