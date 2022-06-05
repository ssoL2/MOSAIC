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
#include <dshow.h>
#include <regex>
#include <vector>

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <pyvirtualcam/native_windows_unity_capture/virtual_output.h>
#include <leptonica/allheaders.h>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>

#pragma comment(lib, "strmiids")

constexpr int BIT_COUNT = 24;
constexpr int NUM_OCR_THREADS = 14;

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


double GetCosineValue(cv::Point pt1, cv::Point pt2, cv::Point pt0)
{
    double dx1 = pt1.x - pt0.x;
    double dy1 = pt1.y - pt0.y;
    double dx2 = pt2.x - pt0.x;
    double dy2 = pt2.y - pt0.y;
    return (dx1 * dx2 + dy1 * dy2) / sqrt((dx1 * dx1 + dy1 * dy1) * (dx2 * dx2 + dy2 * dy2) + 1e-10);
}

HRESULT EnumerateDevices(REFGUID category, IEnumMoniker** ppEnum)
{
    // Create the System Device Enumerator.
    ICreateDevEnum* pDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));

    if (SUCCEEDED(hr))
    {
        // Create an enumerator for the category.
        hr = pDevEnum->CreateClassEnumerator(category, ppEnum, 0);
        if (hr == S_FALSE)
        {
            hr = VFW_E_NOT_FOUND;  // The category is empty. Treat as an error.
        }
        pDevEnum->Release();
    }
    return hr;
}


/** @brief  사용 가능한 카메라 정보 획득
*   현재 사용 가능한 카메라 이름을 STDOUT으로 출력
*
*   @param[in]  pEnum       EnumerateDevices에 사용할 Device Enumerator
*   @return     void
*/
void DisplayDeviceInformation(IEnumMoniker* pEnum)
{
    IMoniker* pMoniker = NULL;

    int num = 0;
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
    {
        IPropertyBag* pPropBag;
        HRESULT hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
        if (FAILED(hr))
        {
            pMoniker->Release();
            continue;
        }

        VARIANT var;
        VariantInit(&var);

        hr = pPropBag->Read(L"Description", &var, 0);
        if (FAILED(hr))
        {
            hr = pPropBag->Read(L"FriendlyName", &var, 0);
        }
        if (SUCCEEDED(hr))
        {
            printf("[%d]: %S@#", num++, var.bstrVal);
            VariantClear(&var);
        }

        hr = pPropBag->Write(L"FriendlyName", &var);

        pPropBag->Release();
        pMoniker->Release();
    }
}


/** @brief 인자로 주어진 이미지로부터 직사각형 인식을 수행, 좌표를 리스트 형태로 반환.
*
*   @param[in]  img         직사각형 인식을 수행할 이미지
*   @return     squareList  인식된 직사각형의 좌하단, 우상단 좌표 튜플 리스트
*/
vector<pair<cv::Point, cv::Point>> GetSquareList(cv::Mat image) {
    const int threshCanny = 1;
    vector<pair<cv::Point, cv::Point>> squareList;

    //흑백 처리
    cv::Mat grayImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);

    //ADAPTIVE_THRESH_GAUSSIAN 기반 이진화 처리
    cv::Mat threImage;
    cv::adaptiveThreshold(grayImage, threImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 61, 7);

    //확장 처리
    cv::Mat dilaImage;
    cv::dilate(threImage, dilaImage, cv::Mat(), cv::Point(-1, -1), 1);

    //테두리 강조 처리
    cv::Mat cannImage;
    cv::Canny(threImage, cannImage, threshCanny, threshCanny * 2);

    //윤곽선 꼭짓점 탐색
    vector<vector<cv::Point>> contourList;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(cannImage, contourList, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    //근사치 적용, 윤곽선 처리
    for (int i = 0; i < contourList.size(); i++) {
        vector<cv::Point> approxList;
        cv::approxPolyDP(contourList[i], approxList, cv::arcLength(contourList[i], true) * 0.02, true);

        //사각형 인식, 인식할 영역의 최대, 최소 넓이 제한
        if (approxList.size() == 4 && cv::isContourConvex(approxList) && cv::contourArea(approxList) > 1000 && cv::contourArea(approxList) < 919600) {

            //직사각형으로 간주할 각도 제한
            double maxCosine = 0;
            for (int j = 2; j < 5; j++)
            {
                double cosine = GetCosineValue(approxList[j % 4], approxList[j - 2], approxList[j - 1]);
                maxCosine = MAX(maxCosine, cosine);
            }

            if (maxCosine < 0.5) {
                int min_x, max_x, min_y, max_y;

                max_x = std::max({ approxList[0].x, approxList[1].x, approxList[2].x, approxList[3].x });
                min_x = std::min({ approxList[0].x, approxList[1].x, approxList[2].x, approxList[3].x });
                max_y = std::max({ approxList[0].y, approxList[1].y, approxList[2].y, approxList[3].y });
                min_y = std::min({ approxList[0].y, approxList[1].y, approxList[2].y, approxList[3].y });

                //좌하단, 우상단 좌표 튜플을 반환할 리스트에 추가
                squareList.push_back(make_pair(cv::Point(min_x, min_y), cv::Point(max_x, max_y)));
            }
        }
    }
    return squareList;
}


/** @brief 캡처된 카메라 이미지로부터 OCR 작업, 개인정보 판별, 마스킹 작업 수행.
*
*   @param[in]  api         tesseract OCR 작업 핸들러
*   @param[in]  opt         마스킹 처리 옵션
*   @return     void
*/
void FilterCameraPI(tesseract::TessBaseAPI* api, int opt) {
    //레이스 컨디션 방지용 최초 시작 랜덤 지연시간 설정
    Sleep(rand() % 100 + 1);

    //직사각형 인식, 직사각형 마스킹
    if (opt == 0) {
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

                //직사각형 인식
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                int x, y, w, h;
                for (int i = 0; i < squareList.size(); i++) {
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //직사각형 마스킹
                    cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                }

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
            semCapture.release();
        }
    }
    //문자열 객체 인식, 문자열 객체 마스킹
    if (opt == 1) {
        // 문자 객체 발견 시 해당 객체 마스킹
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

                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                Boxa* boxes = api->GetComponentImages(tesseract::RIL_SYMBOL, true, NULL, NULL);
                for (int j = 0; j < boxes->n; j++) {
                    BOX* box = boxaGetBox(boxes, j, L_CLONE);
                    int x = box->x;
                    int y = box->y;
                    int w = box->w;
                    int h = box->h;

                    if (w * h < 280000) {
                        cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                    }

                    boxDestroy(&box);
                }
                /*
                * 직사각형 인식 코드 끝
                ***/
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

            semCapture.release();
        }
    }
    //직사각형 인식, 직사각형 내 개인정보 문자열 마스킹
    if (opt == 2) {
        // 직사각형 인식, OCR 수행 후 마스킹
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

                cv::Mat ocrImage;

                //직사각형 인식
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                //이미지 전처리: 흑백처리
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);

                //OCR 이미지 지정
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                for (int i = 0; i < squareList.size(); i++) {
                    int x, y, w, h;
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //OCR 수행할 직사각형 영역 지정
                    api->SetRectangle(x, y, w, h);

                    //OCR 수행
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
                                    cv::rectangle(originImage,
                                        cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                        cv::Scalar(0, 0, 0), -1);
                                }
                            }
                        } while (ri->Next(level));
                    }
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
    }
    //모든 영역 인식, 모든 영역 내 개인정보 문자열 마스킹
    if (opt == 3) {
        while (true)
        {
            semInputQueue.acquire();
            //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
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
                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;


                /***
                * 전체 인식 코드 시작
                */
                cv::Mat gray_image;
                cv::cvtColor(originImage, gray_image, cv::COLOR_BGR2GRAY);

                ocrImage = gray_image;

                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                api->Recognize(0);

                tesseract::ResultIterator* ri = api->GetIterator();
                tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
                if (ri != 0) {
                    do {
                        float conf = ri->Confidence(level);
                        if (conf > 60) {
                            //const char* word = ri->GetUTF8Text(level);
                            string word = ri->GetUTF8Text(level);
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            //cv::rectangle(origin_image_clone, cv::Rect(cv::Point(x1 * multiple_ratio, y1 * multiple_ratio), cv::Point(x2 * multiple_ratio, y2 * multiple_ratio)), cv::Scalar(0, 0, 0), -1);
                            cv::rectangle(originImage, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(0, 0, 0), -1);
                            //printf("word: '%s';  \tconf: %.2f;\n", word, conf);
                        }
                    } while (ri->Next(level));
                }
                /*
                * 전체 인식 코드 끝
                ***/

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

            semCapture.release();
        }
    }

    return;
}


/** @brief 캡처된 카메라 이미지로부터 OCR 작업, 개인정보 판별, 마스킹 작업 수행, 버퍼 기능 대응
*
*   @param[in]  api         tesseract OCR 작업 핸들러
*   @param[in]  opt         마스킹 처리 옵션
*   @return     void
*/
void FilterCameraPI_BUFFER(tesseract::TessBaseAPI* api, int opt) {
    //레이스 컨디션 방지용 최초 시작 랜덤 지연시간 설정
    Sleep(rand() % 100 + 1);

    //직사각형 인식, 직사각형 마스킹
    if (opt == 0) {
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

                //직사각형 인식
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                int x, y, w, h;
                for (int i = 0; i < squareList.size(); i++) {
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //직사각형 마스킹
                    cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                }

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    //문자열 객체 인식, 문자열 객체 마스킹
    if (opt == 1) {
        // 문자 객체 발견 시 해당 객체 마스킹
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

                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                Boxa* boxes = api->GetComponentImages(tesseract::RIL_SYMBOL, true, NULL, NULL);
                for (int j = 0; j < boxes->n; j++) {
                    BOX* box = boxaGetBox(boxes, j, L_CLONE);
                    int x = box->x;
                    int y = box->y;
                    int w = box->w;
                    int h = box->h;

                    if (w * h < 280000) {
                        cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                    }

                    boxDestroy(&box);
                }
                /*
                * 직사각형 인식 코드 끝
                ***/
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

        }
    }
    //직사각형 인식, 직사각형 내 개인정보 문자열 마스킹
    if (opt == 2) {
        // 직사각형 인식, OCR 수행 후 마스킹
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

                cv::Mat ocrImage;

                //직사각형 인식
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                //이미지 전처리: 흑백처리
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);

                //OCR 이미지 지정
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                for (int i = 0; i < squareList.size(); i++) {
                    int x, y, w, h;
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //OCR 수행할 직사각형 영역 지정
                    api->SetRectangle(x, y, w, h);

                    //OCR 수행
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
                                    cv::rectangle(originImage,
                                        cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                        cv::Scalar(0, 0, 0), -1);
                                }
                            }
                        } while (ri->Next(level));
                    }
                }
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    //모든 영역 인식, 모든 영역 내 개인정보 문자열 마스킹
    if (opt == 3) {
        while (true)
        {
            semInputQueue.acquire();
            //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
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
                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;


                /***
                * 전체 인식 코드 시작
                */
                cv::Mat gray_image;
                cv::cvtColor(originImage, gray_image, cv::COLOR_BGR2GRAY);
                cv::adaptiveThreshold(gray_image, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 51, 11);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                api->Recognize(0);

                tesseract::ResultIterator* ri = api->GetIterator();
                tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
                if (ri != 0) {
                    do {
                        float conf = ri->Confidence(level);
                        if (conf > 50) {
                            string word = ri->GetUTF8Text(level);
                            if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                                std::cout << word << std::endl;
                                int x1, y1, x2, y2;
                                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                                cv::rectangle(originImage,
                                    cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                    cv::Scalar(0, 0, 0), -1);
                                /*
                                cv::rectangle(ocrImage,
                                    cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                    cv::Scalar(0, 0, 0), -1);
                                */

                            }
                        }
                    } while (ri->Next(level));
                }
                /*
                * 전체 인식 코드 끝
                ***/

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                /*
                outputQueue.push(make_pair(frameCounter, ocrImage));
                */
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    return;
}


bool FilterCameraPI_DEBUG(tesseract::TessBaseAPI* api) {
    while (true)
    {
        semInputQueue.acquire();
        //printf("do_OCR_fake Thread: %d\n", std::this_thread::get_id());
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

            vector<pair<cv::Point, cv::Point>> rectangle_points = GetSquareList(origin_image_clone);

            semOutputQueue.acquire();
            outputQueue.push(make_pair(frame_counter, origin_image_clone));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }
        semCapture.release();
    }
    return true;
}


/** @brief 가상 카메라로 마스킹 처리된 이미지 출력.
*
*   @param[in]    vCam      tesseract OCR 작업 핸들러
*   @return       void
*/
void ExportVirtualCam(VirtualOutput* vCam) {
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //가상 카메라로 이미지 출력
                vCam->send(image.data);
                currentFrameCounter++;
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


/** @brief 가상 카메라로 마스킹 처리된 이미지 출력, 버퍼 기능 대응
*
*   @param[in]    vCam      tesseract OCR 작업 핸들러
*   @return       void
*/
void ExportVirtualCam_BUFFER(VirtualOutput* vCam, int fps, int bufferSize) {
    bool queueSizeFlag = false;
    while (true)
    {
        printf("outputQueue.size()=%zd\n", outputQueue.size());
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

                //가상 카메라로 이미지 출력
                vCam->send(image.data);

                //초당 프레임 조정
                Sleep(1000 / fps);
                currentFrameCounter++;
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



/** @brief 주어진 카메라에서 이미지 캡처.
*
*   @param[in]    fCounter  현재까지 캡처한 이미지 개수
*   @param[in]    width     캡처 카메라 사양, 너비
*   @param[in]    height    캡처 카메라 사양, 높이
*   @param[in]    fps       캡처 카메라 사양, 초당 캡처 프레임
*   @param[in]    camNumber 캡처할 카메라 번호
*   @return       bool      오류 발생 시 false 반환
*/
bool CaptureCamera(int* fCounter, int width, int height, int fps, int camNumber) {
    //카메라 작동 확인
    cv::VideoCapture capture(camNumber, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        cout << "선택된 카메라를 사용할 수 없습니다." << endl;
        return false;
    }

    //카메라 사양 설정
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FPS, fps);

    while (true)
    {
        //사용할 메모리 최대 용량 제한
        //!TODO: 삭제해도 별 상관 없을듯? 검토 필요
        if (inputQueue.size() > 512) {
            continue;
        }

        //이미지 캡처
        cv::Mat image;
        image.create(height, width, CV_8UC3);
        if (!capture.grab()) {
            cout << "Failed to grab frame!" << endl;
            continue;
        }
        if (!capture.retrieve(image)) {
            cout << "Failed to retrieve frame!" << endl;
            continue;
        }

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, image));
        (*fCounter)++;
        semInputQueue.release();

        //메모리 용량 관리 목적
        //개인정보 처리 스레드가 끝날 때, semCapture를 릴리즈
        semCapture.acquire();
    }
    return true;
}


/** @brief 주어진 카메라에서 이미지 캡처, 버퍼 기능 대응
*
*   @param[in]    fCounter  현재까지 캡처한 이미지 개수
*   @param[in]    width     캡처 카메라 사양, 너비
*   @param[in]    height    캡처 카메라 사양, 높이
*   @param[in]    fps       캡처 카메라 사양, 초당 캡처 프레임
*   @param[in]    camNumber 캡처할 카메라 번호
*   @return       bool      오류 발생 시 false 반환
*/
bool CaptureCamera_VIDEO(int* fCounter, int width, int height, int fps, int camNumber) {
    //카메라 작동 확인
    cv::VideoCapture capture(camNumber, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        cout << "선택된 카메라를 사용할 수 없습니다." << endl;
        return false;
    }

    //카메라 사양 설정
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FPS, fps);

    while (true)
    {
        //이미지 캡처
        cv::Mat image;
        image.create(height, width, CV_8UC3);
        if (!capture.grab()) {
            cout << "Failed to grab frame!" << endl;
            continue;
        }
        if (!capture.retrieve(image)) {
            cout << "Failed to retrieve frame!" << endl;
            continue;
        }

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, image));
        (*fCounter)++;
        semInputQueue.release();


        Sleep(1000 / fps);
    }
    return true;
}


int main(int argc, char* argv[])
{
    // C:\Users\Eungyu\source\repos\miniprj-masking-pi\external\share\tessdata
    if (!argv[1]) {
        std::cout << "데이터셋 파일이 존재하는 경로를 첫번째 인자에 입력해주세요." << std::endl;
        return 1;
    }
    const char* TESSDATA_PATH = argv[1];


    if (!argv[2]) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum;

            hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
            if (SUCCEEDED(hr))
            {
                DisplayDeviceInformation(pEnum);
                pEnum->Release();
            }
            CoUninitialize();
        }
        return 1;
    }


    char* p;
    long cam = strtol(argv[2], &p, 10);
    if (*p != '\0') {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum;

            hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
            if (SUCCEEDED(hr))
            {
                DisplayDeviceInformation(pEnum);
                pEnum->Release();
            }
            CoUninitialize();
        }
        return 1;
    }

    if (!argv[3]) {
        std::cout << "개인정보 필터링 방법 번호를 세번째 인자에 입력해주세요." << std::endl;
        std::cout << "[0]: 직사각형 필터링" << std::endl;
        std::cout << "[1]: 문자 객체 필터링" << std::endl;
        std::cout << "[2]: 직사각형에 한정하여 개인정보 필터링" << std::endl;
        std::cout << "[3]: 모든 영역에 대하여 개인정보 필터링" << std::endl;

        return 1;
    }

    char* q;
    long mask_opiton = strtol(argv[3], &q, 10);
    if (*q != '\0') {
        std::cout << "개인정보 필터링 방법 번호를 세번째 인자에 입력해주세요." << std::endl;
        return 1;
    }

    if (!argv[4]) {
        std::cout << "사용할 버퍼 프레임 개수를 네번째 인자에 입력해주세요." << std::endl;
        return 1;
    }

    char* r;
    long _bufferSize = strtol(argv[4], &r, 10);
    if (*r != '\0') {
        std::cout << "사용할 버퍼 프레임 개수를 네번째 인자에 입력해주세요." << std::endl;
        return 1;
    }


    if (!argv[5]) {
        std::cout << "사용할 데이터베이스 경로를 다섯번째 인자에 입력해주세요." << std::endl;
        return 1;
    }

    SQLite::Database db(argv[5], SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    InitPICheckerRegex(&db, _reList, _reStrList);
    InitPICheckerUser(&db, _incList, _excList);


    int width = 1280;
    int height = 720;
    int fps = 15;
    int bufferSize = (int)_bufferSize;
    int fourcc = cv::VideoWriter::fourcc('2', '4', 'B', 'G');
    VirtualOutput* vo = new VirtualOutput(width, height, fps, fourcc, "Unity Video Capture");
    std::cout << "가상 카메라 " << vo->device() << "를 이용하여 출력합니다." << std::endl;

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

    int frame_counter = 1;

    //캡처 스레드 시작, 비동기
    /*
    std::future<void> threadCapture = std::async(
        std::launch::async,
        [&frame_counter, width, height, fps, cam]() {
            CaptureCamera(&frame_counter, width, height, fps, cam);
        });
    */

    //개인정보 처리 스레드 시작, 비동기
    //!TODO: [장착된 CPU의 스레드 개수 - 1]만큼 스레드 실행
    /*
    auto ocr1 = std::async(std::launch::async, [api1, mask_opiton]() { FilterCameraPI(api1, mask_opiton); });
    auto ocr2 = std::async(std::launch::async, [api2, mask_opiton]() { FilterCameraPI(api2, mask_opiton); });
    auto ocr3 = std::async(std::launch::async, [api3, mask_opiton]() { FilterCameraPI(api3, mask_opiton); });
    auto ocr4 = std::async(std::launch::async, [api4, mask_opiton]() { FilterCameraPI(api4, mask_opiton); });
    auto ocr5 = std::async(std::launch::async, [api5, mask_opiton]() { FilterCameraPI(api5, mask_opiton); });
    auto ocr6 = std::async(std::launch::async, [api6, mask_opiton]() { FilterCameraPI(api6, mask_opiton); });
    auto ocr7 = std::async(std::launch::async, [api7, mask_opiton]() { FilterCameraPI(api7, mask_opiton); });
    auto ocr8 = std::async(std::launch::async, [api8, mask_opiton]() { FilterCameraPI(api8, mask_opiton); });
    */


    //가상 카메라 출력 스레드 시작, 비동기
    /*
    std::future<void> threadExport = std::async(
        std::launch::async, [vo]() {
            ExportVirtualCam(vo);
        });
    */

    std::future<void> threadCapture = std::async(
        std::launch::async,
        [&frame_counter, width, height, fps, cam]() {
            CaptureCamera_VIDEO(&frame_counter, width, height, fps, cam);
        });
    auto ocr1 = std::async(std::launch::async, [api1, mask_opiton]() { FilterCameraPI_BUFFER(api1, mask_opiton); });
    auto ocr2 = std::async(std::launch::async, [api2, mask_opiton]() { FilterCameraPI_BUFFER(api2, mask_opiton); });
    auto ocr3 = std::async(std::launch::async, [api3, mask_opiton]() { FilterCameraPI_BUFFER(api3, mask_opiton); });
    auto ocr4 = std::async(std::launch::async, [api4, mask_opiton]() { FilterCameraPI_BUFFER(api4, mask_opiton); });
    auto ocr5 = std::async(std::launch::async, [api5, mask_opiton]() { FilterCameraPI_BUFFER(api5, mask_opiton); });
    auto ocr6 = std::async(std::launch::async, [api6, mask_opiton]() { FilterCameraPI_BUFFER(api6, mask_opiton); });
    auto ocr7 = std::async(std::launch::async, [api7, mask_opiton]() { FilterCameraPI_BUFFER(api7, mask_opiton); });
    auto ocr8 = std::async(std::launch::async, [api8, mask_opiton]() { FilterCameraPI_BUFFER(api8, mask_opiton); });
    auto ocr9 = std::async(std::launch::async, [api9, mask_opiton]() { FilterCameraPI_BUFFER(api9, mask_opiton); });
    auto ocr10 = std::async(std::launch::async, [api10, mask_opiton]() { FilterCameraPI_BUFFER(api10, mask_opiton); });
    auto ocr11 = std::async(std::launch::async, [api11, mask_opiton]() { FilterCameraPI_BUFFER(api11, mask_opiton); });
    auto ocr12 = std::async(std::launch::async, [api12, mask_opiton]() { FilterCameraPI_BUFFER(api12, mask_opiton); });
    auto ocr13 = std::async(std::launch::async, [api13, mask_opiton]() { FilterCameraPI_BUFFER(api13, mask_opiton); });
    auto ocr14 = std::async(std::launch::async, [api14, mask_opiton]() { FilterCameraPI_BUFFER(api14, mask_opiton); });
    auto ocr15 = std::async(std::launch::async, [api15, mask_opiton]() { FilterCameraPI_BUFFER(api15, mask_opiton); });
    std::future<void> threadExport = std::async(
        std::launch::async, [vo, fps, bufferSize]() {
            ExportVirtualCam_BUFFER(vo, fps, bufferSize);
        });

    _CrtDumpMemoryLeaks();
    return 0;
}