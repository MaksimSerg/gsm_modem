#include <stdio.h>
#include <cstring>
#include <string>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <sys/file.h>


const char LOG_FILE[]   = "log.txt";
const char LOCK_FILE[]  = "pid.lock";
const int  CALL_TIMEOUT = 25;

const char AT[]        = "AT\r\n";
const char AT_CMGF[]   = "AT+CMGF=1\r\n";
const char AT_CHUP[]   = "AT+CHUP\r\n";


std::string intToString ( int value )
{
	std::ostringstream ss;
	ss << value;
	return ss.str();
}

// установка блокировки на файл для поочередной работы с модемом
int set_lock()
{
	int s = -1;
	struct flock fl;

	if ((s=open (LOCK_FILE, O_RDWR | O_CREAT)) < 0) {
		return 0;
	}

	fl.l_whence = 0;
	fl.l_start  = 0;
	fl.l_len    = 0;
	fl.l_type   = F_WRLCK;

	// todo - можно повесить процесс
	while ( fcntl(s, F_SETLKW, &fl) != 0 ) sleep (1);

	return s;
}

// снятие блокировки с файла
int set_unlock(int s)
{
	if (s > 0) {
		close (s);
	}
	
	return 0;
}

const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d\t%X", &tstruct);

    return buf;
}

int log(std::string text)
{
	std::string current_date = currentDateTime();
	std::ofstream file;
	file.open (LOG_FILE, std::ios_base::app);
	file << current_date + "\t" + text + "\n";
	file.close();
	return 0;
}

// найти модем вернуть его адрес
const std::string get_tty_port_name()
{
	return "/dev/ttyUSB1";
}


int main(int argc, char* argv[])
{
	int lock_file = set_lock();

	log( "---------------------------------" );

	std::string phone;

	if (argc > 2) {
		if ( strcmp(argv[1], "call") == 0) {
			phone = std::string(argv[2]);
		} else {
			log( "phone number is undefined" );
			return 0;
		}
	} else {
		log( "phone number is undefined" );
		return 0;
	}

	log("new call: " + phone);
	
	std::string tty_port_name = get_tty_port_name();

	int serial_port = open(tty_port_name.c_str(), O_RDWR);

	struct termios tty;
	memset(&tty, 0, sizeof tty);

	if(tcgetattr(serial_port, &tty) != 0) {
	    log( "Error " + intToString(errno) + " from tcgetattr: " + strerror(errno) );
	    return 0;
	}

	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag |= CS8;
	tty.c_cflag &= CRTSCTS;
	tty.c_cflag |= CREAD | CLOCAL;
	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;
	tty.c_lflag &= ~ECHOE;
	tty.c_lflag &= ~ECHONL;
	tty.c_lflag &= ~ISIG;
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);
	tty.c_oflag &= ~OPOST;
	tty.c_oflag &= ~ONLCR;
	tty.c_cc[VTIME] = 100;
	tty.c_cc[VMIN] = 0;

	cfsetispeed(&tty, B9600);
	cfsetospeed(&tty, B9600);

	if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
	    log( "Error " + intToString(errno) + " from tcsetattr: " + strerror(errno) );
	    return 0;
	}

	write(serial_port, AT, sizeof AT);

	write(serial_port, AT_CMGF, sizeof AT_CMGF);

	std::string at_call_msg = "ATD+" + phone + ";\r\n";
	write(serial_port, at_call_msg.c_str(), at_call_msg.size());

	char read_buf [1024];

	time_t current_time = time(NULL);

	while (time(NULL) - current_time < CALL_TIMEOUT) {
		memset(&read_buf, '\0', sizeof(read_buf));
		int num_bytes = read(serial_port, &read_buf, sizeof(read_buf));

		if (num_bytes < 0) {
		    log( std::string("Error reading: ") + strerror(errno) );
		    break;
		}

		std::string atd_result(read_buf);

		if (atd_result.find("NO DIALTONE") != std::string::npos) {
			log( "Нет сигнала" );
			break;
		}
		if (atd_result.find("VOICE NO CARRIER : ") != std::string::npos) {
			log( "Номер занят/нет ответа/сбросили" );
			break;
		}
		/*if (atd_result.find("VOICE NO CARRIER : 17") != std::string::npos) {
			log( "Повесили трубку" );
			break;
		}*/
		/*if (atd_result.find("VOICE NO CARRIER : 19") != std::string::npos) {
			log( "Нет ответа" );
			break;
		}*/
		if (atd_result.find("NO ANSWER") != std::string::npos) {
			log( "Нет ответа" );
			break;
		}
		if (atd_result.find("ANSWER") != std::string::npos) {
			log( "Вызов принят" );

			// сбрасываем
			write(serial_port, AT_CHUP, sizeof AT_CHUP);
			log( "Сбросили" );
			break;
		}
		if (atd_result.find("RINGBACK") != std::string::npos) {
			log( "Гудки..." );
		}
	}

	write(serial_port, AT_CHUP, sizeof AT_CHUP);
	close(serial_port);
	log( "close" );

	set_unlock(lock_file);

    return 0;
}