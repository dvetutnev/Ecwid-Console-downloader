#include <iostream>
#include <functional>
#include <memory>

using namespace std;

#include "task.h"
#include <sstream>

class Functor
{
public:
  Functor() = default;
  Functor(const Functor&) = delete;
  Functor& operator= (const Functor) = delete;
  void operator() (const char* a)
  {
    cout << "Functor called, arg: " << a << endl;
  }
};

int main(int argc, char *argv[])
{
  cout << "Hello World!" << endl;

  std::function<void()> a = []()
  {
      cout << "functor a called" << endl;
  };

  auto b = a;

  b();
  a();

  std::shared_ptr<Functor> fp = std::make_shared<Functor>();
  fp->operator()("from shared_ptr");
  std::function<void(const char*)> ff = [fp](const char* a) mutable { fp->operator()(a); };
  fp = nullptr;
  ff("first std::function");
  //auto ff2 = std::move(ff);
  auto ff2(ff);
  ff2("copy std::function");
  ff("second std::function");


  std::string s1{"Ahhh, my kraft!"};
  std::string s2{"Second string"};
  std::string s3;

  Task t1{s1, s2};

  cout << "t1: " << t1.uri << " " << t1.fname << endl;

  Task t2{ std::move(s1), std::move(s2) };

  cout << "t2: " << t2.uri << " " << t2.fname << endl;
  cout << "s1: " << s1 << " s2: " << s2 << endl;

  std::stringstream ss;
  ss << "xxxx" << " " << "yyyy" << '\n' << "zzz";

  std::getline(ss, s1);
  std::getline(ss, s2);
  cout << s1 << std::endl;
  cout << s2 << std::endl;


  return 0;
}
