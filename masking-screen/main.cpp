#include <stdio.h>
#include <iostream>
#include <crtdbg.h>
#include <Windows.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <future>
#include <semaphore>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <queue>
#include <filesystem>
#include <regex>
#include <vector>

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>


constexpr int BIT_COUNT = 24;
constexpr int NUM_OCR_THREADS = 14;

int monitor_selected = 0;
int monitorCount = 0;
int monitorWidth = 0;
int monitorHeight = 0;
int monitorLeft = 0;
int monitorTop = 0;

using namespace std;
using namespace concurrency;

struct compare {
    bool operator()(pair<int, cv::Mat>& frame1, pair<int, cv::Mat>& frame2) {
        return frame1.first > frame2.first;
    }
};

/** @brief 전역변수
*   bs_input, bs_output: 이미지 저장용 큐의 레이스 컨디션 관리
*   bs_end: 메모리 용량 관리
*
*   input_queue: 이미지 입력/처리 큐
*   output_queue: 결과 이미지 입력/출력 큐
*
*   currentFrameCounter: 현재까지 출력된 이미지 개수
*/
std::binary_semaphore semInputQueue(1);
std::binary_semaphore semOutputQueue(1);
std::binary_semaphore semCapture(1);

queue<pair<int, cv::Mat>> inputQueue;
priority_queue<pair<int, cv::Mat>, vector<pair<int, cv::Mat>>, compare> outputQueue;

int currentFrameCounter = 1;

std::vector<std::regex> _reList;
std::vector<std::string> _reStrList;
std::vector<std::string> _incList;
std::vector<std::string> _excList;


/** @brief 주어진 정규식, 사용자 정의 포함, 제외 단어 리스트를 기반으로 개인정보를 검출.
*
*   @param[in]  word        개인정보 검출 대상 단어
*   @param[in]  reList      개인정보 검출에 사용할 정규식 리스트
*   @param[in]  reStrList   정규식 리스트에 대응하는 정규식 문자열 리스트
*   @param[in]  incList     개인정보 검출에 사용할 사용자 정의 포함 단어 리스트
*   @param[in]  excList     개인정보 검출에 사용할 사용자 정의 제외 단어 리스트
*   @return     bool        개인정보 검출에 성공하였다면 true, 실패하였다면 false 반환
*/
bool CheckPI(std::string word,
    std::vector<std::regex>& reList,
    std::vector<std::string> reStrList,
    std::vector<std::string>& incList,
    std::vector<std::string>& excList) {

    //사용자 정의 포함 단어 리스트를 기준으로 개인정보 검출
    for (int i = 0; i < incList.size(); i++) {
        //존재한다면, true 반환
        if ((word.find(incList.at(i)) != std::string::npos)) {
            return true;
        }
    }

    //사용자 정의 제외 단어 리스트를 기준으로 개인정보 검출
    for (int i = 0; i < excList.size(); i++) {
        //존재한다면, false 반환
        if ((word.find(excList.at(i)) != std::string::npos)) {
            return false;
        }
    }

    //정규식 리스트를 기준으로 개인정보 검출
    std::smatch match;
    for (int i = 0; i < reList.size(); i++) {
        //존재한다면, true 반환
        if (std::regex_search(word, match, reList.at(i))) {
            std::cout << reStrList[i] << " 에서 일치\n";
            return true;
        }
    }

    //모든 종류의 개인정보 검출 시도에서 결과가 없는 경우, false 반환
    return false;
}


/** @brief 개인정보 검출에 사용할 정규식 리스트 초기화
*
*   @param[in]  db          개인정보 검출에 사용할 정규식이 존재하는 데이터베이스
*   @param[out] reList      개인정보 검출에 사용할 정규식 리스트
*   @param[out] reStrList   정규식 리스트에 대응하는 정규식 문자열 리스트
*   @return     void
*/
void InitPICheckerRegex(SQLite::Database* db, std::vector<std::regex>& reList, std::vector<std::string>& reStrList) {
    std::vector<std::string> regexInputList;

    SQLite::Statement cmd(*db, "SELECT * FROM regex");
    while (cmd.executeStep())
    {
        regexInputList.push_back(cmd.getColumn(0));
    }

    std::vector<std::string>::iterator it;
    for (it = regexInputList.begin(); it != regexInputList.end(); ++it) {
        try {
            reList.push_back(std::regex(*it));
            reStrList.push_back(*it);
        }
        catch (std::exception& e) {
            std::cout << *it << "에서 정규식 객체 생성 실패\n";
            std::cout << e.what() << "\n\n";
        }
    };
}


/** @brief 개인정보 검출에 사용할 사용자 정의 포함, 제외 단어 리스트 초기화
*
*   @param[in]  db          개인정보 검출에 사용할 사용자 정의 포함, 제외 단어가 존재하는 데이터베이스
*   @param[out] incList     개인정보 검출에 사용할 정규식 리스트
*   @param[out] excList     정규식 리스트에 대응하는 정규식 문자열 리스트
*   @return     void
*/
void InitPICheckerUser(SQLite::Database* db, std::vector<std::string>& incList, std::vector<std::string>& excList) {
    //userdefined_include 초기화
    SQLite::Statement cmd1(*db, "SELECT * FROM userdefined_include");
    while (cmd1.executeStep())
    {
        incList.push_back(cmd1.getColumn(0));
    }

    //userdefined_exclude 초기화
    SQLite::Statement cmd2(*db, "SELECT * FROM userdefined_exclude");
    while (cmd2.executeStep())
    {
        excList.push_back(cmd2.getColumn(0));
    }
}


/** @brief 캡처된 화면 이미지로부터 OCR 작업, 개인정보 판별, 마스킹 작업 수행.
*
*   @param[in]  api         tesseract OCR 작업 핸들러
*   @return     void
*/
void FilterScreenPI(tesseract::TessBaseAPI* api, int resizeHeight) {
    //레이스 컨디션 방지용 최초 시작 랜덤 지연시간 설정
    Sleep(rand() % 100 + 1);

    while (true)
    {
        semInputQueue.acquire();
        if (!inputQueue.empty()) {
            int frameCounter = inputQueue.front().first;
            cv::Mat originImage = inputQueue.front().second;
            if (originImage.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();

            //리사이즈된 이미지 좌표 설정 대응 비율 계산
            double imageRatio = originImage.rows / (double)originImage.cols;
            double multipleRatio = originImage.rows / (double)resizeHeight;

            //이미지 전처리: 리사이즈, 흑백처리, 이진처리
            cv::Mat ocrImage;
            cv::resize(originImage, ocrImage, cv::Size(resizeHeight / imageRatio, resizeHeight), 0, 0, cv::INTER_CUBIC);
            cv::cvtColor(ocrImage, ocrImage, cv::COLOR_BGR2GRAY);
            cv::adaptiveThreshold(ocrImage, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 11, 0);

            //OCR 이미지 지정 및 수행
            api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
            api->Recognize(0);

            //단어 단위 인식
            tesseract::ResultIterator* ri = api->GetIterator();
            tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
            if (ri != 0) {
                do {
                    float conf = ri->Confidence(level);
                    if (conf > 30) {
                        string word = ri->GetUTF8Text(level);

                        // 개인정보 검출 시, 해당 영역 마스킹
                        if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            cv::rectangle(
                                originImage,
                                cv::Rect(
                                    cv::Point(x1 * multipleRatio, y1 * multipleRatio),
                                    cv::Point(x2 * multipleRatio, y2 * multipleRatio)),
                                cv::Scalar(0, 0, 0),
                                -1);
                        }
                    }
                } while (ri->Next(level));
            }
            semOutputQueue.acquire();
            outputQueue.push(make_pair(frameCounter, originImage));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }

        //메모리 용량 관리 목적
        //개인정보 처리 작업이 끝난 뒤, 캡처 실행
        semCapture.release();
    }
    return;
}


/** @brief 캡처된 화면 이미지로부터 OCR 작업, 개인정보 판별, 마스킹 작업 수행, 비디오 생성 기능 대응
*
*   @param[in]  api         tesseract OCR 작업 핸들러
*   @return     void
*/
void FilterScreenPI_BUFFER(tesseract::TessBaseAPI* api) {
    //레이스 컨디션 방지용 최초 시작 랜덤 지연시간 설정
    Sleep(rand() % 100 + 1);

    while (true)
    {
        semInputQueue.acquire();
        if (!inputQueue.empty()) {
            int frameCounter = inputQueue.front().first;
            cv::Mat originImage = inputQueue.front().second;
            if (originImage.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();


            //이미지 전처리: 흑백처리, 이진처리
            cv::Mat ocrImage = originImage.clone();
            cv::cvtColor(ocrImage, ocrImage, cv::COLOR_BGR2GRAY);
            //cv::adaptiveThreshold(ocrImage, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 7, 0);

            //OCR 이미지 지정 및 수행
            api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
            api->Recognize(0);

            //단어 단위 인식
            tesseract::ResultIterator* ri = api->GetIterator();
            tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
            if (ri != 0) {
                do {
                    float conf = ri->Confidence(level);
                    if (conf > 50) {
                        string word = ri->GetUTF8Text(level);

                        // 개인정보 검출 시, 해당 영역 마스킹
                        if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                            printf("%s\n", word);
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            cv::rectangle(
                                originImage,
                                cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(0, 0, 0), -1);
                        }
                    }
                } while (ri->Next(level));
            }
            semOutputQueue.acquire();
            outputQueue.push(make_pair(frameCounter, originImage));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }
    }
    return;
}


bool FilterCameraPI_DEBUG(tesseract::TessBaseAPI* api) {
    srand(time(0));
    std::stringstream ss;
    ss << std::this_thread::get_id();
    uint64_t id = std::stoull(ss.str());
    int random_time = rand() % id % 500 + 1;
    Sleep(random_time);
    while (true)
    {
        semInputQueue.acquire();
        //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
        if (!inputQueue.empty()) {
            int frame_counter = inputQueue.front().first;
            cv::Mat origin_image_clone = inputQueue.front().second;
            if (origin_image_clone.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();

            cv::Mat ocr_image;
            double aspect_ratio = origin_image_clone.rows / (double)origin_image_clone.cols;
            int resize_image_height = 1600;
            double multiple_ratio = origin_image_clone.rows / (double)resize_image_height;
            cv::resize(origin_image_clone, ocr_image, cv::Size(resize_image_height / aspect_ratio, resize_image_height), 0, 0, cv::INTER_CUBIC);
            cv::cvtColor(ocr_image, ocr_image, cv::COLOR_BGR2GRAY);
            cv::adaptiveThreshold(ocr_image, ocr_image, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 3, 5);

            semOutputQueue.acquire();
            outputQueue.push(make_pair(frame_counter, ocr_image));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }

        semCapture.release();
    }
}


/** @brief 가상 디스플레이로 마스킹 처리된 이미지 출력.
*
*   @return       void
*/
void ExportVirtualScreen() {
    //지정된 좌표에 존재하는 가상 디스플레이에 전체 화면으로 띄우게끔 창 속성 설정
    string wName = "Press ESC to stop.";
    cv::namedWindow(wName, cv::WINDOW_NORMAL);
    cv::moveWindow(wName, monitorLeft + monitorWidth + 1, monitorTop - 1);
    cv::setWindowProperty(wName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //가상 디스플레이로 이미지 출력
                cv::imshow(wName, image);
                currentFrameCounter++;

                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return;
}


/** @brief 가상 디스플레이로 마스킹 처리된 이미지 출력, 비디오 생성 기능 대응
*
*   @return       void
*/
void ExportVirtualScreen_BUFFER(int fps, int bufferSize) {
    //지정된 좌표에 존재하는 가상 디스플레이에 전체 화면으로 띄우게끔 창 속성 설정
    string wName = "Press ESC to stop.";
    cv::namedWindow(wName, cv::WINDOW_NORMAL);
    cv::moveWindow(wName, monitorLeft + monitorWidth + 1, monitorTop - 1);
    cv::setWindowProperty(wName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    bool queueSizeFlag = false;
    while (true)
    {
        //printf("outputQueue.size()=%zd\n", outputQueue.size());
        if (!queueSizeFlag) {
            if (outputQueue.size() > bufferSize) {
                queueSizeFlag = true;
            }
            continue;
        }

        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //가상 디스플레이로 이미지 출력
                cv::imshow(wName, image);
                Sleep(1000 / fps);
                currentFrameCounter++;

                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return;
}

/***
* 테스트용
*/
bool do_show_fake() {
    string window_name = "Press ESC to stop.";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    //cv::moveWindow(window_name, monitor_capture_left + monitor_capture_width + 1, monitor_capture_top - 1);
    //cv::setWindowProperty(window_name, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                //printf("cur=%d\tqueue=%d\n", current_frame_counter, output_queue.top().first);
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();
                cv::imshow(window_name, image);
                //printf("cols=%d\trows=%d\n", image.cols, image.rows);
                currentFrameCounter++;
                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return false;
}

/** @brief  주 디스플레이에서 이미지 캡처.
*
*   @param[in]  fCounter    현재까지 캡처한 이미지 개수
*   @param[in]  width       캡처 디스플레이 사양, 너비
*   @param[in]  height      캡처 디스플레이 사양, 높이
*   @param[in]  bi          캡처 비트맵 이미지 헤더
*   @return     void
*/
void CaptureScreen(int* fCounter, int width, int height, BITMAPINFOHEADER bi) {
    while (true)
    {
        //사용할 메모리 최대 용량 제한
        //!TODO: 삭제해도 별 상관 없을듯? 검토 필요
        if (inputQueue.size() > 512) {
            continue;
        }

        //핸들러 생성
        HDC hScreen = GetDC(NULL);
        HDC hDC = CreateCompatibleDC(hScreen);

        //캡처 후 메모리에 저장할 비트맵 이미지 생성
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        HGDIOBJ old_obj = SelectObject(hDC, hBitmap);

        //비트맵 이미지 형태로 디스플레이 캡처
        BOOL bRet = BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

        //OpenCV 이미지로 변환
        cv::Mat mat;
        mat.create(height, width, CV_8UC3);
        GetDIBits(hDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        //리소스 관리 및 회수
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, mat));
        (*fCounter)++;
        semInputQueue.release();

        //메모리 용량 관리 목적
        //개인정보 처리 스레드가 끝날 때, semCapture를 릴리즈
        semCapture.acquire();
    }
    return;
}


/** @brief  주 디스플레이에서 이미지 캡처, 비디오 생성 기능 대응
*
*   @param[in]  fCounter    현재까지 캡처한 이미지 개수
*   @param[in]  width       캡처 디스플레이 사양, 너비
*   @param[in]  height      캡처 디스플레이 사양, 높이
*   @param[in]  bi          캡처 비트맵 이미지 헤더
*   @return     void
*/
void CaptureScreen_BUFFER(int* fCounter, int width, int height, BITMAPINFOHEADER bi, int fps) {
    while (true)
    {
        //핸들러 생성
        HDC hScreen = GetDC(NULL);
        HDC hDC = CreateCompatibleDC(hScreen);

        //캡처 후 메모리에 저장할 비트맵 이미지 생성
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        HGDIOBJ old_obj = SelectObject(hDC, hBitmap);

        //비트맵 이미지 형태로 디스플레이 캡처
        BOOL bRet = BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

        //OpenCV 이미지로 변환
        cv::Mat mat;
        mat.create(height, width, CV_8UC3);
        GetDIBits(hDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        //리소스 관리 및 회수
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, mat));
        (*fCounter)++;
        semInputQueue.release();
        Sleep(1000 / fps);
    }
    return;
}


/** @brief  모니터 사양 획득용 콜백 함수.
*   모니터의 너비, 높이, 좌상단 좌표 획득
*   EnumDisplayMonitors의 콜백 함수로 사용
*/
BOOL CALLBACK MonitorEnumProc(HMONITOR hmonitor, HDC hdc, LPRECT lprect, LPARAM lpara) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hmonitor, &mi);
    if (monitor_selected == monitorCount) {
        monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
        monitorLeft = mi.rcMonitor.left;
        monitorTop = mi.rcMonitor.top;
        std::cout << "캡처할 모니터 해상도: " << monitorWidth << "*" << monitorHeight << std::endl;
        std::cout << "캡처할 모니터 좌상단 좌표: (" << monitorLeft << ", " << monitorTop << ")" << std::endl;
    }
    monitorCount++;
    return true;
}


int main(int argc, char* argv[])
{
    // C:\Users\Eungyu\source\repos\miniprj-masking-pi\external\share\tessdata
    if (!argv[1]) {
        std::cout << "데이터셋 파일이 존재하는 경로를 첫번째 인자에 입력해주세요." << endl;
        return 1;
    }
    const char* TESSDATA_PATH = argv[1];

    if (!argv[2]) {
        std::cout << "사용할 버퍼 프레임 개수를 두번째 인자에 입력해주세요." << std::endl;
        return 1;
    }

    char* p;
    long _bufferSize = strtol(argv[2], &p, 10);
    if (*p != '\0') {
        std::cout << "사용할 버퍼 프레임 개수를 두번째 인자에 입력해주세요." << std::endl;
        return 1;
    }


    if (!argv[3]) {
        std::cout << "사용할 데이터베이스 경로를 세번째 인자에 입력해주세요." << std::endl;
        return 1;
    }

    SQLite::Database db(argv[3], SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    InitPICheckerRegex(&db, _reList, _reStrList);
    InitPICheckerUser(&db, _incList, _excList);


    monitor_selected = 0;
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, NULL);

    // 캡처할 스크린 크기 저장용 변수
    RECT tScreen;

    // 데스크탑 핸들러 생성 및 스크린 크기 얻어오기
    const HWND hDesktop = GetDesktopWindow();
    GetWindowRect(hDesktop, &tScreen);

    int width = tScreen.right;
    int height = tScreen.bottom;

    // 출력할 이미지 크기 설정
    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = BIT_COUNT;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    tesseract::TessBaseAPI* api1 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api2 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api3 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api4 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api5 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api6 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api7 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api8 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api9 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api10 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api11 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api12 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api13 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api14 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api15 = new tesseract::TessBaseAPI();


    /**
    * tesseract::OEM_DEFAULT
    * tesseract::OEM_LSTM_ONLY
    * tesseract::OEM_TESSERACT_ONLY
    * tesseract::OEM_TESSERACT_LSTM_COMBINED
    */
    if (api1->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        std::cout << "입력된 경로 " << argv[1] << "에 데이터셋 파일이 존재하지 않습니다." << endl;
        std::cout << "다음 링크에서 데이터셋을 다운로드 받으세요." << endl;
        std::cout << "eng: https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata" << endl;
        std::cout << "kor: https://github.com/tesseract-ocr/tessdata_fast/raw/main/kor.traineddata" << endl;
        return 1;
    }
    if (api2->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api3->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api4->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api5->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api6->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api7->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api8->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api9->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api10->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api11->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api12->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api13->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api14->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api15->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }

    /**
    * tesseract::PSM_SINGLE_BLOCK
    * ...
    */
    tesseract::PageSegMode PSM_MODE = static_cast<tesseract::PageSegMode>(tesseract::PSM_SINGLE_BLOCK);
    api1->SetPageSegMode(PSM_MODE);
    api2->SetPageSegMode(PSM_MODE);
    api3->SetPageSegMode(PSM_MODE);
    api4->SetPageSegMode(PSM_MODE);
    api5->SetPageSegMode(PSM_MODE);
    api6->SetPageSegMode(PSM_MODE);
    api7->SetPageSegMode(PSM_MODE);
    api8->SetPageSegMode(PSM_MODE);
    api9->SetPageSegMode(PSM_MODE);
    api10->SetPageSegMode(PSM_MODE);
    api11->SetPageSegMode(PSM_MODE);
    api12->SetPageSegMode(PSM_MODE);
    api13->SetPageSegMode(PSM_MODE);
    api14->SetPageSegMode(PSM_MODE);
    api15->SetPageSegMode(PSM_MODE);

    int resizeHeight = height;
    int frame_counter = 1;
    int fps = 10;
    int bufferSize = (int)_bufferSize;

    //캡처 스레드 시작, 비동기
    std::future<void> thread_capture = std::async(
        std::launch::async,
        [&frame_counter, width, height, bi, fps]() {
            CaptureScreen_BUFFER(&frame_counter, width, height, bi, fps);
        });

    //개인정보 처리 스레드 시작, 비동기
    //!TODO: [장착된 CPU의 스레드 개수 - 1]만큼 스레드 실행
    auto ocr1 = std::async(std::launch::async, [api1]() { FilterScreenPI_BUFFER(api1); });
    auto ocr2 = std::async(std::launch::async, [api2]() { FilterScreenPI_BUFFER(api2); });
    auto ocr3 = std::async(std::launch::async, [api3]() { FilterScreenPI_BUFFER(api3); });
    auto ocr4 = std::async(std::launch::async, [api4]() { FilterScreenPI_BUFFER(api4); });
    auto ocr5 = std::async(std::launch::async, [api5]() { FilterScreenPI_BUFFER(api5); });
    auto ocr6 = std::async(std::launch::async, [api6]() { FilterScreenPI_BUFFER(api6); });
    auto ocr7 = std::async(std::launch::async, [api7]() { FilterScreenPI_BUFFER(api7); });
    auto ocr8 = std::async(std::launch::async, [api8]() { FilterScreenPI_BUFFER(api8); });
    auto ocr9 = std::async(std::launch::async, [api9]() { FilterScreenPI_BUFFER(api9); });
    auto ocr10 = std::async(std::launch::async, [api10]() { FilterScreenPI_BUFFER(api10); });
    auto ocr11 = std::async(std::launch::async, [api11]() { FilterScreenPI_BUFFER(api11); });
    auto ocr12 = std::async(std::launch::async, [api12]() { FilterScreenPI_BUFFER(api12); });
    auto ocr13 = std::async(std::launch::async, [api13]() { FilterScreenPI_BUFFER(api13); });
    auto ocr14 = std::async(std::launch::async, [api14]() { FilterScreenPI_BUFFER(api14); });
    auto ocr15 = std::async(std::launch::async, [api15]() { FilterScreenPI_BUFFER(api15); });

    //가상 카메라 출력 스레드 시작, 비동기
    std::future<void> thread_show = std::async(
        std::launch::async, [fps, bufferSize]() {
            ExportVirtualScreen_BUFFER(fps, bufferSize);
        });

    /*
    auto ocr8 = std::async(std::launch::async, [api8, resizeHeight]() { FilterScreenPI(api8, resizeHeight); });
    auto ocr9 = std::async(std::launch::async, [api9, resizeHeight]() { FilterScreenPI(api9, resizeHeight); });
    auto ocr10 = std::async(std::launch::async, [api10, resizeHeight]() { FilterScreenPI(api10, resizeHeight); });
    auto ocr11 = std::async(std::launch::async, [api11, resizeHeight]() { FilterScreenPI(api11, resizeHeight); });
    auto ocr12 = std::async(std::launch::async, [api12, resizeHeight]() { FilterScreenPI(api12, resizeHeight); });
    auto ocr13 = std::async(std::launch::async, [api13, resizeHeight]() { FilterScreenPI(api13, resizeHeight); });
    auto ocr14 = std::async(std::launch::async, [api14, resizeHeight]() { FilterScreenPI(api14, resizeHeight); });
    auto ocr15 = std::async(std::launch::async, [api15, resizeHeight]() { FilterScreenPI(api15, resizeHeight); });
    */

    /*
    auto ocr1 = std::async(std::launch::async, [api1]() { do_OCR_fake(api1); });
    auto ocr2 = std::async(std::launch::async, [api2]() { do_OCR_fake(api2); });
    auto ocr3 = std::async(std::launch::async, [api3]() { do_OCR_fake(api3); });
    auto ocr4 = std::async(std::launch::async, [api4]() { do_OCR_fake(api4); });
    auto ocr5 = std::async(std::launch::async, [api5]() { do_OCR_fake(api5); });
    auto ocr6 = std::async(std::launch::async, [api6]() { do_OCR_fake(api6); });
    auto ocr7 = std::async(std::launch::async, [api7]() { do_OCR_fake(api7); });
    auto ocr8 = std::async(std::launch::async, [api8]() { do_OCR_fake(api8); });
    */

    _CrtDumpMemoryLeaks();
    return 0;
}