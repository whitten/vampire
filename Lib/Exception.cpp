/**
 * Class Exception.cpp. Implements Vampire exceptions.
 *
 * @since 03/12/2003, Manchester
 */

#include "Int.hpp"

#include "Exception.hpp"

namespace Lib
{

int Exception::s_exceptionCounter=0;

Exception::Exception (const char* msg, int line)
  : _message((string(msg)+": "+Int::toString(line)).c_str())
{ s_exceptionCounter++; }

/**
 * Write a description of the exception to a stream.
 */
void Exception::cry (ostream& str)
{
  str << _message << "\n";
} // Exception::cry


/**
 * Write a description of the exception to a stream.
 */
void UserErrorException::cry (ostream& str)
{
  str << "User error: " << _message << "\n";
} // UserErrorException::cry

/**
 * Write a description of the exception to a stream.
 */
void InvalidOperationException::cry (ostream& str)
{
  str << "Invalid operation: " << _message << "\n";
} // UserErrorException::cry

/**
 * Write a description of the exception to a stream.
 */
void NotImplementedException::cry (ostream& str)
{
  str << "Not implemented at " << file << ":" << line << "\n";
} // UserErrorException::cry


}
