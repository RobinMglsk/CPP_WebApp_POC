#include "crow_all.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <boost/filesystem.hpp>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/instance.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;
using mongocxx::cursor;

void sendFile(crow::response &res, std::string fileName, std::string contentType)
{
    std::ifstream in("../public/" + fileName, std::ifstream::in);
    if (in)
    {
        std::ostringstream contents;
        contents << in.rdbuf();
        in.close();
        res.set_header("Content-Type", contentType);
        res.write(contents.str());
    }
    else
    {
        res.code = crow::status::NOT_FOUND;
    }

    res.end();
}

void sendHTML(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/html");
}

void sendImage(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "image/jpeg");
}

void sendScript(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/javascript");
}

void sendStyle(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/css");
}

crow::mustache::rendered_template getView(const std::string &filename, crow::mustache::context &x)
{
    return crow::mustache::load(filename + ".html").render(x);
}

int main(int argc, char *argv[])
{
    crow::SimpleApp app;
    crow::mustache::set_global_base("../public/");

    mongocxx::instance inst{};
    std::string mongoConnect = std::string(getenv("MONGODB_URI"));
    mongocxx::client conn{mongocxx::uri{mongoConnect}};
    auto collection = conn["cppweb"]["contacts"];

    CROW_ROUTE(app, "/contact/<string>")
    ([&collection](std::string email)
     {
        auto doc = collection.find_one(make_document(kvp("email", email)));
        crow::json::wvalue dto;
        dto["contact"] = crow::json::load(bsoncxx::to_json(doc.value().view()));

        return getView("contact", dto); });

    CROW_ROUTE(app, "/contacts")
    ([&collection]()
     {
        mongocxx::options::find opts;
        opts.skip(9);
        opts.limit(10);
        auto docs = collection.find({}, opts);

        crow::json::wvalue dto;
        std::vector<crow::json::rvalue> contacts;
        contacts.reserve(10);

        for(auto doc : docs){
            contacts.push_back(crow::json::load(bsoncxx::to_json(doc)));
        }

        dto["contacts"] = contacts;

        return getView("contacts", dto); });

    CROW_ROUTE(app, "/")
    ([](const crow::request &req, crow::response &res)
     { sendHTML(res, "index.html"); });

    CROW_ROUTE(app, "/styles/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendStyle(res, "/styles/" + filename); });

    CROW_ROUTE(app, "/scripts/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendScript(res, "/scripts/" + filename); });

    CROW_ROUTE(app, "/images/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendImage(res, "/images/" + filename); });

    char *port = getenv("PORT");
    const uint16_t iPort = static_cast<uint16_t>(port != NULL ? std::stoi(port) : 18080);
    std::cout << "PORT = " << iPort << "\n";

    app.port(iPort).multithreaded().run();
}