// IPCClientCpp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h> 
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include "addressbook.pb.h"

using namespace std;

#define BUFSIZE 512
#define READ_TIMEOUT 500      // milliseconds
#define ERROR_STATE -1
#define SUCCESS_STATE 1

VOID AddPerson(string &);

wstring ConvertStrToWStr(const string& s)
{
	int len;
	int slength = (int)s.length() + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	std::wstring r(buf);
	delete[] buf;
	return r;
}

// For sync i/o operations
HANDLE CreateFileSync(LPTSTR lpszPipename) {
	HANDLE hPipe;
	hPipe = CreateFile(
				lpszPipename,   // pipe name 
				GENERIC_READ |  // read and write access 
				GENERIC_WRITE,
				0,              // no sharing 
				NULL,           // default security attributes
				OPEN_EXISTING,  // opens existing pipe 
				0,              // default attributes 
				NULL);          // no template file 
	return hPipe;
}

// Uses FILE_FLAG_OVERLAPPED flag for async i/o
HANDLE CreateFileAsync(LPTSTR lpszPipename) {
	HANDLE hPipe;
	hPipe = CreateFile(
				lpszPipename,		   // pipe name 
				GENERIC_READ |		   // read and write access 
				GENERIC_WRITE,
				0,					   // no sharing 
				NULL,				   // default security attributes
				OPEN_EXISTING,		   // opens existing pipe 
				FILE_FLAG_OVERLAPPED,  // async i/o
				NULL);				   // no template file 
	return hPipe;
}

int readAsync(HANDLE hPipe, TCHAR  chBuf[]) {
	printf("Async read started\n");
	DWORD dwRead;
	BOOL fWaitingOnRead = FALSE;
	BOOL fSuccess = FALSE;
	OVERLAPPED osReader = { 0 };

	// Create the overlapped event. Must be closed before exiting
	// to avoid a handle leak.
	osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (osReader.hEvent == NULL)
		return ERROR_STATE;  // Error creating overlapped event. Abort.
	
	//  Issues a read operation
	do {
		fSuccess = ReadFile(hPipe,
			chBuf,
			BUFSIZE * sizeof(TCHAR),
			&dwRead,
			&osReader);

		if (!fSuccess) {
			if (GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_MORE_DATA)
			{	
				_tprintf(TEXT("GLE=%d\n"), GetLastError());
				return ERROR_STATE;
			} else if (GetLastError() != ERROR_MORE_DATA)
				fWaitingOnRead = TRUE;
		}
		else {
			// read completed immediately
			_tprintf(TEXT("\"%s\"\n"), chBuf);
		}
	} while (!fSuccess && !fWaitingOnRead);
	
	// Detect the completion of an overlapped read operation
	DWORD dwRes;
	if (fWaitingOnRead) {
		dwRes = WaitForSingleObject(osReader.hEvent, READ_TIMEOUT);
		switch (dwRes)
		{
		// Read completed.
		case WAIT_OBJECT_0:
			// Detect completion of the operation
			if (!GetOverlappedResult(hPipe, &osReader, &dwRead, FALSE))
				return ERROR_STATE;
			else
				// Read completed successfully.
				_tprintf(TEXT("\"%s\"\n"), chBuf);

			//  Reset flag so that another opertion can be issued.
			fWaitingOnRead = FALSE;
			break;

		case WAIT_TIMEOUT:
			// Operation isn't complete yet. fWaitingOnRead flag isn't
			// changed since I'll loop back around, and I don't want
			// to issue another read until the first one finishes.
			//
			// Good time to do some background work.
			break;

		default:
			// Error in the WaitForSingleObject; abort.
			// This indicates a problem with the OVERLAPPED structure's
			// event handle.
			throw::runtime_error("WaitForSingleObject failed! :(");
			return ERROR_STATE;
		}
	}
	return SUCCESS_STATE;
}

int readSync(HANDLE hPipe, TCHAR chBuf[])
{
	printf("Sync read started\n");
	DWORD dwRead;
	BOOL fSuccess = FALSE;

	//  Issue a read operation
	do
	{
		// Read from the pipe. 
		fSuccess = ReadFile(
			hPipe,					  // pipe handle 
			chBuf,					  // buffer to receive reply 
			BUFSIZE * sizeof(TCHAR),  // size of buffer 
			&dwRead,				  // number of bytes read 
			NULL);                    // not overlapped 

		if (!fSuccess && GetLastError() != ERROR_MORE_DATA)
			break;

		_tprintf(TEXT("\"%s\"\n"), chBuf);
	} while (!fSuccess);  // repeat loop if ERROR_MORE_DATA 

	if (!fSuccess)
	{
		_tprintf(TEXT("ReadFile from pipe failed. GLE=%d\n"), GetLastError());
		return ERROR_STATE;
	}
	return SUCCESS_STATE;
}

BOOL writeSync(HANDLE hPipe, DWORD cbToWrite, LPCWSTR LpSerializedString)
{
	printf("Sync write started\n");
	DWORD cbWritten;
	BOOL fSuccess = FALSE;
	
	fSuccess = WriteFile(
		hPipe,                  // pipe handle 
		LpSerializedString,       // message 
		cbToWrite,              // message length 
		&cbWritten,             // bytes written 
		NULL);                  // not overlapped 

	if (!fSuccess)
	{
		_tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"), GetLastError());
		return FALSE;
	}
	return TRUE;
}

BOOL writeAsync(HANDLE hPipe, DWORD cbToWrite, LPCWSTR LpSerializedString)
{
	printf("Async write started\n");
	OVERLAPPED osWrite = { 0 };
	DWORD dwWritten;
	BOOL fRes;

	// Create this writes OVERLAPPED structure hEvent.
	osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (osWrite.hEvent == NULL)
		// Error creating overlapped event handle.
		return FALSE;

	// Issue write.
	if (!WriteFile(hPipe, LpSerializedString, cbToWrite, &dwWritten, &osWrite)) {
		if (GetLastError() != ERROR_IO_PENDING) {
			_tprintf(TEXT("WriteFile to pipe failed. GLE=%d\n"), GetLastError());
			fRes = FALSE;
		}
		else {
			// Write is pending.
			if (!GetOverlappedResult(hPipe, &osWrite, &dwWritten, TRUE))
			{
				_tprintf(TEXT("WriteFile to pipe pending. GLE=%d\n"), GetLastError());
				fRes = FALSE;
			}
			else
				// Write operation completed successfully.
				fRes = TRUE;
		}
	}
	else
		// WriteFile completed immediately.
		fRes = TRUE;

	CloseHandle(osWrite.hEvent);
	return fRes;
}

int _tmain(int argc, TCHAR *argv[])
{
	HANDLE hPipe;
	TCHAR  chBuf[BUFSIZE];
	BOOL   fSuccess = FALSE;
	DWORD  cbRead, cbToWrite, dwMode;
	LPTSTR lpszPipename = const_cast<LPTSTR>(TEXT("\\\\.\\pipe\\goldengate"));

	// Try to open a named pipe; wait for it, if necessary.

	while (1)
	{
		hPipe = CreateFileSync(lpszPipename);

		// Break if the pipe handle is valid. 

		if (hPipe != INVALID_HANDLE_VALUE)
			break;

		// Exit if an error other than ERROR_PIPE_BUSY occurs. 

		if (GetLastError() != ERROR_PIPE_BUSY)
		{
			_tprintf(TEXT("Could not open pipe. GLE=%d\n"), GetLastError());
			return -1;
		}

		// All pipe instances are busy, so wait for 20 seconds. 

		if (!WaitNamedPipe(lpszPipename, 20000))
		{
			printf("Could not open pipe: 20 second wait timed out.");
			return -1;
		}
	}

	// The pipe connected; change to message-read mode. 

	dwMode = PIPE_READMODE_MESSAGE;
	fSuccess = SetNamedPipeHandleState(
		hPipe,    // pipe handle 
		&dwMode,  // new pipe mode 
		NULL,     // don't set maximum bytes 
		NULL);    // don't set maximum time 
	if (!fSuccess)
	{
		_tprintf(TEXT("SetNamedPipeHandleState failed. GLE=%d\n"), GetLastError());
		return -1;
	}

	// Send a message to the pipe server. 
	string serialized_book;

	// Add a person to the address book
	AddPerson(serialized_book);
	
	// Convert string to LPCWSTR
	std::wstring sTemp = ConvertStrToWStr(serialized_book);
	LPCWSTR LpSerializedBook = sTemp.c_str();

	cbToWrite = (lstrlen(LpSerializedBook) + 1) * sizeof(TCHAR);
	_tprintf(TEXT("Sending %d byte message: \"%s\"\n"), cbToWrite, LpSerializedBook);

	BOOL writeState = writeSync(hPipe, cbToWrite, LpSerializedBook);
	if (!writeState)
	{
		return ERROR_STATE;
	}

	printf("\nMessage sent to server, receiving reply as follows:\n");

	int readState = readSync(hPipe, chBuf);
	if (readState == -1) {
		return readState;
	}

	printf("\nPress [ENTER] to terminate connection and exit.");
	_getch();

	CloseHandle(hPipe);

	return 0;
}

VOID AddPerson(string &book) {
	tutorial::AddressBook address_book;
	tutorial::Person* person = address_book.add_people();

	cout << "Enter person ID number: ";
	int id;
	cin >> id;
	person->set_id(id);
	cin.ignore(256, '\n');

	cout << "Enter name: ";
	getline(cin, *person->mutable_name());

	cout << "Enter email address (blank for none): ";
	string email;
	getline(cin, email);
	if (!email.empty()) {
		person->set_email(email);
	}

	while (true) {
		cout << "Enter a phone number (or leave blank to finish): ";
		string number;
		getline(cin, number);
		if (number.empty()) {
			break;
		}

		tutorial::Person::PhoneNumber* phone_number = person->add_phones();
		phone_number->set_number(number);

		cout << "Is this a mobile, home, or work phone? ";
		string type;
		getline(cin, type);
		if (type == "mobile") {
			phone_number->set_type(tutorial::Person::MOBILE);
		}
		else if (type == "home") {
			phone_number->set_type(tutorial::Person::HOME);
		}
		else if (type == "work") {
			phone_number->set_type(tutorial::Person::WORK);
		}
		else {
			cout << "Unknown phone type.  Using default." << endl;
		}
	}

	address_book.SerializeToString(&book);
}
