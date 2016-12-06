#include <Windows.h>
#include <time.h>
#include <list>
#pragma comment(lib, "ws2_32.lib")

using namespace std;


//хендл главного окна
HWND hWindow;

//хендл лейбла с выполнением
HWND hLabelPercentComplete;

//листбокс с клиентами
HWND hClientListBox;
//кнопка запуска
HWND hBtn;

//хендлы текстобксов интеграла
HWND fromTB, toTB, stepTB, limTB;

//треды мерцания и приема
HANDLE hColorThread = 0;
HANDLE hAcceptThread = 0;

#define C_COLOR 1
#define BTN_PRESS 2

//флаг того, что вычисления в процессе
bool running = 0;

//пределы интеграла и загрузки
double from, to, step, lim;

//прототипы функций
void serverMain();
void acceptThread(void* param);
void clientThread(void* param);
void setComplete(int perc, int sec);

//число клиентов
int clientCount = 0;
//размер очереди
int totalQueSize = 0;

//конечный результат вычислений
double result = 0;

int lgefunc(int)
{
	int korn = 0;
		korn++;

	return 0;
}

//структура с данными клиента для добавления в список
typedef struct _clientData
{
	SOCKET s;
	SOCKADDR_IN addr;
} clientData;

//структура интеграла
typedef struct _Integral
{
	double from;
	double to;
	double step;
	double load;
} Integral;

//сокет приема подключений
SOCKET serverSocket;
sockaddr_in serverSaddr;

//очередь заявок
list<Integral> que;

//крит секции для доступа
CRITICAL_SECTION listBoxCriticalSection;
CRITICAL_SECTION queRetreiveCriticalSection;
CRITICAL_SECTION addResCriticalSection;

//добавляет результат вычислений через критическую секцию
void addResSafe(double val)
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	result += val;
	LeaveCriticalSection(&queRetreiveCriticalSection);


}
//считает, сколько всего операций будет произведено
double countOperations()
{
	double lims = to - from;
	return lims / step;
}

//получает следующий интеграл или ноль, если очередь кончилась
Integral getNextIg()
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	if (que.size() > 0)
	{

		Integral ig = que.front();
		que.pop_front();
		LeaveCriticalSection(&queRetreiveCriticalSection);
		return ig;
	}
	//если очередь кончилась, вернем ноль
	Integral r = { 0 };
	LeaveCriticalSection(&queRetreiveCriticalSection);

	return r;
}

//получает размер оставшейся очереди заявок
int getQueSizeSafe()
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	int res = que.size();
	LeaveCriticalSection(&queRetreiveCriticalSection);
	return res;
}

//добавляет заявку в конец очереди, безопасно
void addLastIg(Integral* ig)
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	que.push_back(*ig);
	LeaveCriticalSection(&queRetreiveCriticalSection);

}


//тред, считающий выполнение
void timerThread()
{
	while (running)
	{
		//получаем количество отсавшихся заявок
		int cnt = totalQueSize - getQueSizeSafe();
		if (totalQueSize == 0)
			totalQueSize = 1;
		double perc = ((double)100 / totalQueSize) * cnt;
		//и еще раз
		int newcnt = totalQueSize - getQueSizeSafe();

		//получаем разницу
		int diffr = newcnt - cnt;
		//замеряем время выполнения одной заявки
		long start = clock();
		while (diffr < 1)
		{
			Sleep(200);
			newcnt = totalQueSize - getQueSizeSafe();
			diffr = newcnt - cnt;
			if (!running)
			{
				return;
			}
		}
		long timer = clock();
		newcnt = totalQueSize - getQueSizeSafe();
		//длительность выполнения одной заявки в секундах
		double duration = (timer - start) / (double)CLOCKS_PER_SEC;
		//количество заявок в секунду
		double packetspersecond = (double)diffr / duration;


		//ставим значение
		setComplete((int)perc, (int)((double)getQueSizeSafe() / packetspersecond));
	}
}


//формирует очередь
list<Integral> formOperations()
{
	//по 5% всех операций будет делать каждый клиент
	unsigned long long ops = countOperations();
	totalQueSize = 0;
	ops++;
	unsigned  long long opsPerClient = ops * (double)0.05;
	if (opsPerClient >= 1000000)
	{
		opsPerClient = 1000000;
	}

	list<Integral> igs;
	double lastfrom = from, lastto = lastfrom;

	double slice = opsPerClient * step;
	while (ops > 0)
	{
		if (ops <= opsPerClient)
		{
			//slice = ops;
			opsPerClient = ops;
		}
		Integral in = { 0 };
		in.step = step;
		in.load = lim;
		in.from = lastfrom;
		in.to = lastfrom + slice;
		lastfrom += slice;
		igs.push_back(in);

		ops -= opsPerClient;
		totalQueSize++;
	}
	return igs;
}

//добавляет клиента в список
void addClientToListBox(sockaddr_in saddr) {
	char buf[32];
	char temp[32];
	TCHAR wbuf[32] = { 0 };
	strcat(strcat(strcat(strcpy(buf, inet_ntoa(saddr.sin_addr)), ":"), itoa(ntohs(saddr.sin_port), temp, 10)), " ");
	mbstowcs(wbuf, buf, 30);
	EnterCriticalSection(&listBoxCriticalSection);
	SendMessage(hClientListBox, LB_ADDSTRING, 0, (LPARAM)wbuf);
	LeaveCriticalSection(&listBoxCriticalSection);

}
//удаляет клиента из списка
void deleteClientFromListBox(sockaddr_in saddr) {
	char buf[32];
	TCHAR wbuf[32];
	char temp[32];
	mbstowcs(wbuf, strcat(strcat(strcpy(buf, inet_ntoa(saddr.sin_addr)), ":"), itoa(ntohs(saddr.sin_port), temp, 10)), 32);

	EnterCriticalSection(&listBoxCriticalSection);
	int num = SendMessage(hClientListBox, LB_FINDSTRING, -1, (LPARAM)wbuf);
	SendMessage(hClientListBox, LB_DELETESTRING, num, 0);
	LeaveCriticalSection(&listBoxCriticalSection);
}

//останавливает вычисления
void stopMain()
{
	SetWindowText(hBtn, L"Го");
	SetWindowLong(fromTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(toTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(stepTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(limTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	running = 0;
	//TerminateThread(hAcceptThread, 0);
	closesocket(serverSocket);

	hAcceptThread = 0;
}
//проверяет параметры, готовит к вычислениям
void preMain()
{
	result = 0;
	from = -1;
	to = -1;
	step = -1;
	lim = -1;
	TCHAR wfrom[25] = { 0 };
	TCHAR wto[25] = { 0 };
	TCHAR wstep[25] = { 0 };
	TCHAR wlim[25] = { 0 };

	GetWindowText(fromTB, wfrom, sizeof(wfrom));
	GetWindowText(toTB, wto, sizeof(wto));
	GetWindowText(stepTB, wstep, sizeof(wstep));
	GetWindowText(limTB, wlim, sizeof(wlim));




	if (wfrom[0] == 0 ||
		wto[0] == 0 ||
		wstep[0] == 0 ||
		wlim[0] == 0)
	{
		MessageBox(0, L"Один из параметров задан неверно или не задан", 0, 0);
		return;
	}

	from = _wtof(wfrom);
	to = _wtof(wto);
	step = _wtof(wstep);
	lim = _wtof(wlim);



	if (!(from != -1 && to != -1 && step != -1 && lim != -1))
	{
		MessageBox(0, L"Один из параметров задан неверно или не задан", 0, 0);
		return;
	}
	running = 1;

	SetWindowText(hBtn, L"Стоп");
	SetWindowLong(fromTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(toTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(stepTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(limTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);


	que = formOperations();

	serverMain();
}

//запуск сервера
void serverMain()
{
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		MessageBox(hWindow, L"Ошибка загрузки Winsock", L"Ошибка", MB_OK);
		return;
	}
	if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"Ошибка создания сокета", L"Ошибка", MB_OK);
		return;
	}
	serverSaddr.sin_family = AF_INET;
	serverSaddr.sin_addr.s_addr = INADDR_ANY;
	serverSaddr.sin_port = htons(1316);
	if (bind(serverSocket, (sockaddr*)&serverSaddr, sizeof(serverSaddr)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"Ошибка связывания сокета", L"Ошибка", MB_OK);
		return;
	}

	listen(serverSocket, 0x100);
	if (hAcceptThread == 0)
	{
		hAcceptThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)acceptThread, &serverSocket, 0, 0);
	}
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)timerThread, 0, 0, 0);

}

HANDLE* hClientWorkers = 0;
//тред, принимающий подключения
void acceptThread(void* param)
{
	SOCKET s = *(SOCKET*)param;
	hClientWorkers = (HANDLE*)malloc(sizeof(HANDLE) * MAXIMUM_WAIT_OBJECTS);
	ZeroMemory(hClientWorkers, sizeof(HANDLE)* MAXIMUM_WAIT_OBJECTS);
	int currclient = 0;
	while (1)
	{
		sockaddr_in saddrClient = { 0 };
		int saddrSize = sizeof(sockaddr_in);
		SOCKET cl = accept(s, (sockaddr*)&saddrClient, &saddrSize);
		//клиент подключился
		if (cl == INVALID_SOCKET)
		{
			if (running)
			{
				//первый клиент, который выполнит все заявки, закроет этот сокет. ошибка 10004 означает выполнение операции на закрытом сокете, что не является именно ошибкой в данном случае.
				if (WSAGetLastError() == 10004 && getQueSizeSafe() == 0)
				{
					break;
				}
				MessageBox(hWindow, L"Ошибка доступа", L"Ошибка", MB_OK);
			}
			return;
		}
		//добавляем его в список
		addClientToListBox(saddrClient);
		clientData cd = { 0 };
		//формируем его данные, это позволит нам удалить его из списка в случае чего
		cd.s = cl;
		cd.addr = saddrClient;

		//если остались необработанные заявки, нагружаем клиента
		if (getQueSizeSafe() > 0)
		{
			hClientWorkers[currclient] = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)clientThread, &cd, 0, 0);
			currclient++;
		}
		//если заявки кончились, останавливаем прием подключений
		else
			break;
	}
	//дожидаемся, пока все клиенты закончат работу
	WaitForMultipleObjects(currclient, hClientWorkers, 1, INFINITE);

	if (getQueSizeSafe() == 0)
	{
		//конец вычислениям
		TCHAR buf[320];
		setComplete((int)100, (int)0);
		swprintf(buf, L"Конец! Рельутат: %f", result);
		stopMain();
		MessageBox(hWindow, buf, L"Конетс", MB_OK);
		
	}
	shutdown(s, 2);
	closesocket(serverSocket);
	WSACleanup();
}

//тред, обрабатываюший каждого клиента
void clientThread(void* param)
{
	clientData cd = *(clientData*)param;
	SOCKET s = cd.s;
	Integral* ig = 0;
	Integral* lastig = 0;

	int nread = 0;
	double resp = 0;
	bool isOk = 1;

	//если от клиента нет ответа в течение 10 секунд, отключим его
	DWORD timeout = 10000;

	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(DWORD)) != 0)
	{
		return;
	}
	//выбираем очередной интеграл
	ig = &getNextIg();
	//если его шаг ноль, значит очередь кончилась, отключим клиента
	if (ig->step == 0)
	{
		closesocket(s);
		deleteClientFromListBox(cd.addr);
		return;
	}
	lastig = ig;
	//отпарвялем ему интеграл
	if (send(s, (char*)ig, sizeof(Integral), 0) < 0)
	{
		isOk = 0;
	}

	if (isOk)
	{

		while (1)
		{
			//получаем ответ
			int nbytes = recv(s, (char*)&resp, sizeof(double), 0);
			if (nbytes <= 0)
			{
				//если соединение оборвалось, была ошибка
				isOk = 0;
				break;
			}
			//добавим его значения к результату
			addResSafe(resp);
			//берем следующий интеграл
			ig = &getNextIg();
			//если его шаг ноль, вычислеия кончились
			if (ig->step == 0)
			{

				ig->step = 0;
				send(s, (char*)ig, sizeof(Integral), 0);
				closesocket(s);
				closesocket(serverSocket);

				break;
			}

			lastig = ig;
			int snd = 0;
			//отпарвялем иннтеграл клиенту, повторяем до конца очрееди
			snd = send(s, (char*)ig, sizeof(Integral), 0);
			if (snd <= 0)
			{
				isOk = 0;
				break;
			}
		}
	}
	if (!isOk)
	{
		//если что то отвалилось, добавляем заявку обратно в очередь
		addLastIg(lastig);
	}
	closesocket(s);
	deleteClientFromListBox(cd.addr);
}
//устанавливает выполнение
void setComplete(int perc, int sec)
{

	TCHAR outBuff[64] = { 0 };
	wsprintf(outBuff, L"%d%%, %d сек", perc, sec);

	SendMessage(hLabelPercentComplete, WM_SETTEXT, 0, (LPARAM)outBuff);
}



//обработчик сообщений
LRESULT CALLBACK windowsProcedure(HWND Window, UINT message, WPARAM wParam, LPARAM lParam) {
	int id = LOWORD(wParam), event = HIWORD(wParam);
	PAINTSTRUCT ps;

	HDC hdc;
	switch (message) {
	case WM_CTLCOLORSTATIC:
	{
		//если производятся вычисления, поставим все цвета в случайный цвет
		HDC hdcStatic = (HDC)wParam;


		//иначе черный
		SetTextColor(hdcStatic, RGB(0, 0, 0));
		SetBkColor(hdcStatic, RGB(255, 255, 255));
		return (INT_PTR)CreateSolidBrush(RGB(255, 255, 255));
	}

	case WM_COMMAND:
		switch (id) {
			//сообщение отрисовки, вызовем редрау
		case C_COLOR:
			RedrawWindow(hWindow, 0, 0, RDW_INVALIDATE | RDW_ERASE);
			break;
			//нажатие копки
		case BTN_PRESS:
			if (!running)
			{
				preMain();
			}
			else
			{
				stopMain();
			}
			break;
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(Window, &ps);
		EndPaint(Window, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(Window, message, wParam, lParam);
	}
	return 0;
}




int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow) {
	//создаем класс
	srand(time(0));
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = 0;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hIcon = 0;
	wcex.hbrBackground = (HBRUSH)COLOR_WINDOWFRAME;
	wcex.lpszMenuName = 0;
	wcex.hIconSm = 0;
	wcex.lpfnWndProc = windowsProcedure;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"Server";
	//регистрируем его
	RegisterClassEx(&wcex);

	//создаем элементы управлеия
	hWindow = CreateWindow(L"Server", L"Server", WS_SYSMENU, 0, 0, 420, 180, NULL, NULL, hInstance, NULL);
	CreateWindowEx(0, L"Static", L"Решено:", WS_VISIBLE | WS_CHILD, 4, 4, 60, 16, hWindow, (HMENU)0, hInstance, 0);
	hLabelPercentComplete = CreateWindowEx(0, L"Static", L"--%, -- sec", WS_VISIBLE | WS_CHILD, 64, 4, 180, 16, hWindow, (HMENU)0, hInstance, 0);

	CreateWindowEx(0, L"Static", L"От:", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 4, 24, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	fromTB = CreateWindowEx(0, L"Edit", L"1", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 24, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"До:", WS_VISIBLE | WS_CHILD, 4, 44, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	toTB = CreateWindowEx(0, L"Edit", L"100000", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 44, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"Шаг:", WS_VISIBLE | WS_CHILD, 4, 64, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	stepTB = CreateWindowEx(0, L"Edit", L"0.001", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 64, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"Макс. загрузка клиента: ", WS_VISIBLE | WS_CHILD, 4, 84, 180, 16, hWindow, (HMENU)0, hInstance, 0);
	limTB = CreateWindowEx(0, L"Edit", L"1", WS_VISIBLE | WS_CHILD | WS_BORDER, 200, 84, 25, 16, hWindow, (HMENU)0, hInstance, 0);


	hClientListBox = CreateWindowEx(0, L"ListBox", L"LBox", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOVSCROLL | LBS_HASSTRINGS, 244, 4, 150, 142, hWindow, (HMENU)0, hInstance, 0);


	hBtn = CreateWindowEx(0, L"Button", L"Старт", WS_VISIBLE | WS_CHILD | WS_BORDER, 4, 116, 220, 20, hWindow, (HMENU)BTN_PRESS, hInstance, 0);


	ShowWindow(hWindow, cmdShow);
	UpdateWindow(hWindow);

	//инициализация критсекций
	InitializeCriticalSection(&listBoxCriticalSection);
	InitializeCriticalSection(&queRetreiveCriticalSection);

	//основной цикл обработки
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}
