#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <vector>
#include <map>
#include <curl/curl.h>
#include <json/json.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <limits>


class LoginSystem {
private:
    std::string validUsername;
    std::string validPassword;
    int maxAttempts;
public:
    LoginSystem(const std::string& username, const std::string& password, int attempts = 10)
        : validUsername(username), validPassword(password), maxAttempts(attempts) {}
    bool authenticate(const std::string& username, const std::string& password) {
        return (username == validUsername && password == validPassword);
    }
    void start() {
        std::string inputUsername;
        std::string inputPassword;
        int attempts = 0;
        std::cout << "Welcome to the Login System!\n";

        while (attempts < maxAttempts) {
            std::cout << "Enter username: ";
            std::getline(std::cin, inputUsername);
            std::cout << "Enter password: ";
            std::getline(std::cin, inputPassword);

            if (authenticate(inputUsername, inputPassword)) {
                std::cout << "Login successful! Welcome, " << inputUsername << "!\n";
                return;
            }
            else {
                attempts++;
                std::cout << "Invalid username or password. "
                    << (maxAttempts - attempts) << " attempts remaining.\n";
            }
        }
        std::cout << "Too many failed attempts. Access denied.\n";
        std::exit(EXIT_FAILURE);
    }
};

// HTTP 数据回调函数
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;

}

// 股票数据获取类
class StockDataFetcher {
public:
    StockDataFetcher(const std::string& apiKey) : apiKey(apiKey) {}

    std::map<std::string, double> fetchIntradayData(const std::string& symbol, const std::string& interval) {
        const std::string url = "https://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY"
            "&symbol=" + symbol + "&interval=" + interval + "&apikey=" + apiKey;
        CURL* curl;
        CURLcode res;
        std::string readBuffer;
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "cURL Error: " << curl_easy_strerror(res) << std::endl;
            }
            curl_easy_cleanup(curl);
        }
        return parseJsonData(readBuffer);
    }
private:
    std::string apiKey;
    std::map<std::string, double> parseJsonData(const std::string& jsonData) {
        Json::CharReaderBuilder readerBuilder;
        Json::Value root;
        std::string errs;
        std::istringstream stream(jsonData);
        std::map<std::string, double> priceData;

        if (Json::parseFromStream(readerBuilder, stream, &root, &errs)) {
            Json::Value timeSeries = root["Time Series (5min)"];
            for (const auto& time : timeSeries.getMemberNames()) {
                priceData[time] = std::stod(timeSeries[time]["1. open"].asString());
            }
        }
        else {
            std::cerr << "Failed to parse JSON: " << errs << std::endl;
        }
        return priceData;
    }
};

//线性回归预测股票
void linearRegression(const std::vector<double>& x, const std::vector<double>& y, double& slope, double& intercept) {

    if (x.size() != y.size() || x.size() < 2) return;

    double sum_x = std::accumulate(x.begin(), x.end(), 0.0);
    double sum_y = std::accumulate(y.begin(), y.end(), 0.0);
    double sum_xy = 0.0;
    double sum_xx = 0.0;

    for (size_t i = 0; i < x.size(); ++i) {
        sum_xy += x[i] * y[i];
        sum_xx += x[i] * x[i];
    }
    size_t n = x.size();
    slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    intercept = (sum_y - slope * sum_x) / n;
}

std::string formatTime(double minutes) {
    int hour = static_cast<int>(minutes) / 60;
    int min = static_cast<int>(minutes) % 60;
    char buffer[100];
    sprintf(buffer, "%02d:%02d", hour, min);
    return std::string(buffer);
}



// 抽象交易者基类
class Trader {
public:
    virtual void configure(double parameter1, double parameter2 = 0.0) = 0;
    virtual double simulateTrading(const std::map<std::string, double>& prices) = 0;
    virtual ~Trader() = default;
};

// 动量交易策略：支持动态阈值、交易限制和手续费版
class MomentumTrader : public Trader {
private:
    double threshold;//动量触发最小价格涨幅
    double transactionFee;//每次交易的手续费
    int maxPositions;//最大允许持仓
    int positions;//当前持仓数量

public:
    MomentumTrader() : threshold(0.01), transactionFee(0.5), maxPositions(10), positions(0) {}

    void configure(double parameter1, double parameter2 = 0.0) override {
        threshold = parameter1;
        transactionFee = parameter2;
    }

    double simulateTrading(const std::map<std::string, double>& prices) override {
        double profit = 0.0;
        double prevPrice = -1;

        for (const auto& [time, price] : prices) {
            if (prevPrice > 0 && price > prevPrice * (1 + threshold) && positions < maxPositions) {
                profit -= price + transactionFee; // 买入
                positions++;
            }
            else if (positions > 0 && price < prevPrice) {
                profit += price - transactionFee; // 卖出
                positions--;
            }
            prevPrice = price;
        }

        // 平仓剩余头寸
        profit += positions * prevPrice - positions * transactionFee;
        return profit;
    }
};

// 均值回归策略支持窗口大小、动态偏差比例版本
class MeanReversionTrader : public Trader {
private:
    int windowSize;  // 移动平均线窗口大小
    double deviationFactor;  // 偏离均值的触发比例
    double transactionFee;  // 交易手续费
    std::deque<double> priceWindow;  // 价格窗口

public:
    MeanReversionTrader() : windowSize(5), deviationFactor(0.02), transactionFee(0.5) {}

    void configure(double parameter1, double parameter2 = 0.0) override {
        windowSize = static_cast<int>(parameter1); // 设置均值窗口大小
        deviationFactor = parameter2;
    }

    double simulateTrading(const std::map<std::string, double>& prices) override {
        double profit = 0.0;

        for (const auto& [time, price] : prices) {
            if (priceWindow.size() == windowSize) {
                double movingAverage = std::accumulate(priceWindow.begin(), priceWindow.end(), 0.0) / windowSize;

                if (price < movingAverage * (1 - deviationFactor)) {
                    profit += (movingAverage - price) - transactionFee; // 超跌买入
                }
                else if (price > movingAverage * (1 + deviationFactor)) {
                    profit += (price - movingAverage) - transactionFee; // 超涨卖出
                }

                priceWindow.pop_front();
            }

            priceWindow.push_back(price);
        }

        return profit;
    }
};

// 突破交易策略：支持动态支撑位和阻力位版本
class BreakoutTrader : public Trader {
private:
    double breakoutFactor;  // 突破触发比例
    double transactionFee;  // 交易手续费
    double breakoutLevel;  // 当前的支撑/阻力位

public:
    BreakoutTrader() : breakoutFactor(0.02), transactionFee(0.5), breakoutLevel(-1) {}

    void configure(double parameter1, double parameter2 = 0.0) override {
        breakoutFactor = parameter1;
        transactionFee = parameter2;
    }

    double simulateTrading(const std::map<std::string, double>& prices) override {
        double profit = 0.0;

        for (const auto& [time, price] : prices) {
            if (breakoutLevel < 0) {
                breakoutLevel = price; //支撑位
            }
            else if (price > breakoutLevel * (1 + breakoutFactor)) {
                profit += (price - breakoutLevel) - transactionFee; // 突破阻力位
                breakoutLevel = price; // 更新阻力位
            }
            else if (price < breakoutLevel * (1 - breakoutFactor)) {
                profit += (breakoutLevel - price) - transactionFee; // 跌破支撑位
                breakoutLevel = price; // 更新支撑位
            }
        }

        return profit;
    }
};

// 策略模拟器类

class StrategySimulator {
public:
    StrategySimulator() {
        strategies.push_back(new MomentumTrader());
        strategies.push_back(new MeanReversionTrader());
        strategies.push_back(new BreakoutTrader());
    }
    ~StrategySimulator() {
        for (auto* strategy : strategies) {
            delete strategy;
        }
    }

    void simulateForStock(const std::string& symbol, const std::map<std::string, double>& priceData) {
        std::cout << "=== Results for Stock: " << symbol << " ===" << std::endl;

        std::vector<std::pair<std::string, Trader*>> strategies = {
            {"Momentum Strategy", new MomentumTrader()},
            {"Mean Reversion Strategy", new MeanReversionTrader()},
            {"Breakout Strategy", new BreakoutTrader()}
        };

        for (const auto& [name, strategy] : strategies) {
            double profit = strategy->simulateTrading(priceData);
            std::cout << name << " Profit: $" << profit << std::endl;
            delete strategy; // 防止内存泄露
        }
        std::cout << "============================" << std::endl;
    }

private:
    std::vector<Trader*> strategies;

};

int main() {
    LoginSystem login("yan", "666");
    login.start();

    std::string apiKey = "9RMC0BFRG7L5ANNS"; // Alpha Vantage API Key

    std::cout << "\nDo you want to use your own API Key? If not we provide a default API Key \nA.I have an API Key.       Any key but A. I need the default API Key:\n";
    char cho;
    std::cin >> cho;
    if (cho == 'A' || cho == 'a') {
        std::cout << "Enter Your own API Key." << std::endl;
        std::cin >> apiKey;
    }
    else {
        std::cout << "You are using default API Key\n" << std::endl;
    }
    char choice;
    char repeat;
    while (true) {
        std::cout << "Please select operation\nA.prediction      B.strategy:";
        std::cin >> choice;
        if (choice == 'A' || choice == 'a') {
            std::cout << "Enter the stock symbol you want to predict." << std::endl;

            std::string symbol;
            std::cin >> symbol;

            StockDataFetcher fetcher(apiKey);
            auto priceData = fetcher.fetchIntradayData(symbol, "5min");

            if (!priceData.empty()) {
                std::vector<double> times;
                std::vector<double> closePrices;

                // 解析时间序列数据
                for (const auto& [time, price] : priceData) {
                    // 使用时间戳转化为连续的分钟数
                    std::stringstream ss(time);
                    std::string dateStr, timeStr;
                    std::getline(ss, dateStr, ' ');
                    std::getline(ss, timeStr);
                    int hours, minutes;
                    sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes);
                    double timeInMinutes = hours * 60 + minutes;

                    times.push_back(timeInMinutes);
                    closePrices.push_back(price);

                    std::cout << "Time: " << formatTime(timeInMinutes) << ", Close Price: " << price << std::endl;
                }

                // 只取最后 10 条数据
                size_t dataSize = times.size();
                size_t startIdx = (dataSize > 10) ? dataSize - 10 : 0;

                std::vector<double> recentTimes(times.begin() + startIdx, times.end());
                std::vector<double> recentClosePrices(closePrices.begin() + startIdx, closePrices.end());

                // 输出用于预测的数据
                std::cout << "\nData used for prediction:" << std::endl;
                for (size_t i = 0; i < recentTimes.size(); ++i) {
                    std::cout << "Time: " << formatTime(recentTimes[i]) << ", Close Price: " << recentClosePrices[i] << std::endl;
                }

                // 进行线性回归计算斜率和截距
                double slope, intercept;
                linearRegression(recentTimes, recentClosePrices, slope, intercept);

                // 输出线性回归结果
                std::cout << "Linear regression result: y = " << slope << "x + " << intercept << std::endl;

                // 使用线性回归模型预测未来的股价
                double futureTime = recentTimes.back();
                std::vector<double> predictions;
                for (int i = 1; i <= 5; ++i) {
                    double predictedClose = slope * (futureTime + i) + intercept;
                    predictions.push_back(predictedClose);
                    std::cout << "Predicted stock price at " << formatTime(futureTime + i) << ": " << predictedClose << std::endl;
                }
            }
            else {
                std::cerr << "No data found for stock: " << symbol << std::endl;
            }

        }
        else if (choice == 'B' || choice == 'b') {
            std::vector<std::string> stockSymbols;

            for (int i = 0; i < 3; ++i) {
                std::string symbol;
                std::cout << "Enter stock symbol" << (i + 1) << ": ";
                std::cin >> symbol;
                stockSymbols.push_back(symbol);
            }

            StockDataFetcher fetcher(apiKey);
            StrategySimulator simulator;

            for (const auto& symbol : stockSymbols) {
                auto priceData = fetcher.fetchIntradayData(symbol, "5min");
                if (!priceData.empty()) {
                    simulator.simulateForStock(symbol, priceData);
                }
                else {
                    std::cerr << "No data found for stock: " << symbol << std::endl;
                }
            }
        }
        else {
            std::cout << "Invalid input, please enter A/B" << std::endl;
        }

        // 询问用户是否再次访问
        std::cout << "Whether to visit again(Y/N)" << std::endl;
        std::cin >> repeat;
        if (repeat == 'N' || repeat == 'n') {
            std::cout << "Program exit, thank you for using！" << std::endl;
            break;
        }
        else if (repeat == 'Y' || repeat == 'y') {
            std::cout << "Return to operation page" << std::endl;
        }
        else {
            std::cout << "Invalid input, exit by default." << std::endl;
            break;
        }
    }

    return 0;
}