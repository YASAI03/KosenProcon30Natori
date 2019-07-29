#include "HTTPCommunication.hpp"
#include "Observer.hpp"
#include "Game.hpp"

std::mutex Procon30::HTTPCommunication::receiveRawMtx;
String Procon30::HTTPCommunication::receiveRawData;

Procon30::CommunicationState Procon30::HTTPCommunication::getState() const
{
	if (!this->future.valid())
		return CommunicationState::Null;
	if (this->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		return CommunicationState::Done;
	return CommunicationState::Connecting;
}

Procon30::ConnectionStatusCode Procon30::HTTPCommunication::getResult()
{
	assert(this->future.valid());
	if (this->future.valid()) {
		CURLcode result = this->future.get();
		uint32 code;
		if (result != CURLE_OK) {
			Logger << U"curl_easy_perform() failed.\nUnixTime(Milli):"
				<< Time::GetMillisecSinceEpoch();
			return ConnectionStatusCode::ConnectionLost;
		}
		{
			std::lock_guard<std::mutex> lock(receiveRawMtx);
			size_t ofset = Min(receiveRawData.indexOf('['), receiveRawData.indexOf('{'));
			code = Parse<uint32>(receiveRawData.substr(9, 3));
			TextWriter writer(receiveJsonPath);
			writer << receiveRawData.substr(ofset);
			writer.close();
		}
		if (code != 200) {
			Logger << U"Status Error:" << code;
			if (code == 401) {
				return ConnectionStatusCode::InvaildToken;
			}
			JSONReader reader(receiveJsonPath);
			String status = reader[U"status"].get<String>();
			uint64 startAtUnixTime = reader[U"startAtUnixTime"].get<uint64>();
			Logger << U"status:" << status << U"\nstartAtUnixTime" << startAtUnixTime;
			if (U"InvalidMatches" == status) {

				return ConnectionStatusCode::InvailedMatches;
			}
			if (U"TooEarly" == status) {

				return ConnectionStatusCode::TooEarly;
			}
			if (U"UnacceptableTime" == status) {

				return ConnectionStatusCode::UnacceptableTime;
			}
		}
		return ConnectionStatusCode::OK;
	}
	return ConnectionStatusCode::Null;
}

size_t Procon30::HTTPCommunication::callbackWrite(char* ptr, size_t size, size_t nmemb, String* stream) {
	std::string buf;
	size_t dataLength = size * nmemb;
	buf.append(ptr, dataLength);
	{
		std::lock_guard<std::mutex> lock(receiveRawMtx);
		stream->append(Unicode::Widen(buf));
	}
	return dataLength;
}

inline String Procon30::HTTPCommunication::getPostData(const FilePath& filePath) {
	TextReader reader(filePath);
	if (!reader) {
		assert("send_json_file_openError");
		exit(1);
	}
	String send = reader.readAll();
	reader.close();
	return send;
}

bool Procon30::HTTPCommunication::checkResult()
{
	if (nowConnecting) {
		switch (getState())
		{
		case CommunicationState::Done:
			switch (getResult())
			{
			case Procon30::ConnectionStatusCode::OK:
				switch (connectionType)
				{
				case Procon30::ConnectionType::Ping:
					Print << U"PingTest:OK";
					break;
				case Procon30::ConnectionType::AllMatchesInfo:


					break;
				case Procon30::ConnectionType::MatchInfomation:

					gotMatchInfomationNum++;
					break;
				case Procon30::ConnectionType::PostAction:
					break;
				case Procon30::ConnectionType::Null:
					break;
				}
				break;
			case Procon30::ConnectionStatusCode::TooEarly:
				break;
			case Procon30::ConnectionStatusCode::UnacceptableTime:
				break;
			case Procon30::ConnectionStatusCode::InvailedMatches:
				break;
			case Procon30::ConnectionStatusCode::InvaildToken:
				break;
			case Procon30::ConnectionStatusCode::ConnectionLost:
				break;
			case Procon30::ConnectionStatusCode::Null:
				break;
			}

			nowConnecting = false;
			return true;
			break;
		case CommunicationState::Null:

			break;
		case CommunicationState::Connecting:

			break;
		}
	}
	return false;
}

void Procon30::HTTPCommunication::setConversionTable(const Array<int>& arr)
{
	for (int32 i = 0; i < matchNum; i++) {
		matchesConversionTable[i] = arr[i];
	}
}

void Procon30::HTTPCommunication::initilizeAllMatchHandles()
{
	for (int32 i = 0; i < matchNum; i++) {
		getMatchHandles[i] = curl_easy_init();
		curl_easy_setopt(getMatchHandles[i], CURLOPT_URL, (U"http://" + host + U"/matches/"+Format(matchesConversionTable[i])).narrow().c_str());
		curl_easy_setopt(getMatchHandles[i], CURLOPT_HTTPHEADER, otherList);
		curl_easy_setopt(getMatchHandles[i], CURLOPT_HEADER, 1L);
		curl_easy_setopt(getMatchHandles[i], CURLOPT_WRITEFUNCTION, callbackWrite);
		curl_easy_setopt(getMatchHandles[i], CURLOPT_WRITEDATA, &receiveRawData);

		postActionHandles[i] = curl_easy_init();
		curl_easy_setopt(postActionHandles[i], CURLOPT_URL, (U"http://" + host + U"/matches/" + Format(matchesConversionTable[i])+U"/action").narrow().c_str());
		curl_easy_setopt(postActionHandles[i], CURLOPT_HTTPHEADER, postList);
		curl_easy_setopt(postActionHandles[i], CURLOPT_HEADER, 1L);
		curl_easy_setopt(postActionHandles[i], CURLOPT_POST, 1L);
		curl_easy_setopt(postActionHandles[i], CURLOPT_WRITEFUNCTION, callbackWrite);
		curl_easy_setopt(postActionHandles[i], CURLOPT_WRITEDATA, &receiveRawData);
	}
}

void Procon30::HTTPCommunication::update()
{
	//読み捨てていいの？
	checkResult();
	if (gotMatchInfomationNum == matchNum) {
		Procon30::Game::HTTPReceived();
		gotMatchInfomationNum = 0;
	}
	if (gotMatchInfomationNum != 0) {
		getMatchInfomation();
	}
}

bool Procon30::HTTPCommunication::pingServerConnectionTest()
{
	if (nowConnecting) return false;
	nowConnecting = true;
	connectionType = ConnectionType::Ping;
	future = std::async(std::launch::async, [&]() {
		return curl_easy_perform(pingHandle);
		});
	return true;
}

bool Procon30::HTTPCommunication::getAllMatchesInfomation()
{
	if (nowConnecting) return false;
	nowConnecting = true;
	connectionType = ConnectionType::AllMatchesInfo;
	future = std::async(std::launch::async, [&]() {
		return curl_easy_perform(matchesInfoHandle);
		});
	return true;
}

bool Procon30::HTTPCommunication::getMatchInfomation()
{
	if (nowConnecting) return false;
	if (gotMatchInfomationNum >= matchNum) return false;
	nowConnecting = true;
	connectionType = ConnectionType::MatchInfomation;
	connectionMatchNumber = gotMatchInfomationNum;
	future = std::async(std::launch::async, [&]() {
		return curl_easy_perform(getMatchHandles[connectionMatchNumber]);
		});
	return true;
}

bool Procon30::HTTPCommunication::checkPostAction()
{
	if (nowConnecting) return false;
	if (buffer->size() == 0) return false;
	nowConnecting = true;
	connectionType = ConnectionType::PostAction;
	FilePath path = buffer->getPath();
	//post_(game[0,1,2])_(turn).jsonを想定
	auto splittedPath = path.split('_');
	int32 gameNum = Parse<int32>(splittedPath[1]);
	String send = getPostData(path);
	curl_easy_setopt(postActionHandles[gameNum], CURLOPT_POSTFIELDSIZE, (long)send.size());
	curl_easy_setopt(postActionHandles[gameNum], CURLOPT_POSTFIELDS, send.c_str());
	future = std::async(std::launch::async, [&]() {
		return curl_easy_perform(postActionHandles[gameNum]);
		});
	return true;
}

void Procon30::HTTPCommunication::jsonDistribute()
{

}

void Procon30::HTTPCommunication::getServer()
{



}

void Procon30::HTTPCommunication::postServer()
{
	FilePath path = buffer->getPath();


}

Procon30::HTTPCommunication::HTTPCommunication()
	:buffer(new SendBuffer())
{
	//Setting Header
	postList = NULL;
	otherList = NULL;
	otherList = curl_slist_append(otherList, (U"Authorization:" + token).narrow().c_str());
	postList = curl_slist_append(postList, "Authorization:procon30_example_token");
	postList = curl_slist_append(postList, "Content-Type: application/json");

	//Handle-initilize
	pingHandle = curl_easy_init();
	curl_easy_setopt(pingHandle, CURLOPT_URL, (U"http://" + host + U"/ping").narrow().c_str());
	curl_easy_setopt(pingHandle, CURLOPT_HTTPHEADER, otherList);
	curl_easy_setopt(pingHandle, CURLOPT_HEADER, 1L);
	curl_easy_setopt(pingHandle, CURLOPT_WRITEFUNCTION, callbackWrite);
	curl_easy_setopt(pingHandle, CURLOPT_WRITEDATA, &receiveRawData);

	matchesInfoHandle = curl_easy_init();
	curl_easy_setopt(matchesInfoHandle, CURLOPT_URL, (U"http://" + host + U"/matches").narrow().c_str());
	curl_easy_setopt(matchesInfoHandle, CURLOPT_HTTPHEADER, otherList);
	curl_easy_setopt(matchesInfoHandle, CURLOPT_HEADER, 1L);
	curl_easy_setopt(matchesInfoHandle, CURLOPT_WRITEFUNCTION, callbackWrite);
	curl_easy_setopt(matchesInfoHandle, CURLOPT_WRITEDATA, &receiveRawData);
}

Procon30::HTTPCommunication::~HTTPCommunication()
{
}

Procon30::HTTPCommunication& Procon30::HTTPCommunication::operator=(const Procon30::HTTPCommunication& right)
{

	return (*this);
}

//Procon30::InformationID::InformationID()
//{
//}

//inline Procon30::InformationID::InformationID(int32 teamid, Array<int32> agentids) : teamID(teamid), AgentIDs(agentids) {};
