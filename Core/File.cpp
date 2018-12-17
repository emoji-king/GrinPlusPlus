#include <Core/File.h>

#include <FileUtil.h>
#include <fstream>

// TODO: Use memory mapped files (For Windows: https://docs.microsoft.com/en-us/windows/desktop/Memory/file-mapping)
File::File(const std::string& path)
	: m_path(path),
	m_bufferIndex(0),
	m_fileSize(0)
{

}

bool File::Load()
{
	std::ifstream file(m_path, std::ios::in | std::ifstream::ate | std::ifstream::binary);
	if (!file.is_open())
	{
		return false;
	}

	m_fileSize = file.tellg();
	m_bufferIndex = m_fileSize;
	file.close();

	return true;
}

bool File::Flush()
{
	if (m_fileSize == m_bufferIndex && m_buffer.empty())
	{
		return true;
	}

	std::ofstream file(m_path, std::ios::out | std::ios::binary | std::ios::app);
	if (!file.is_open())
	{
		return false;
	}

	file.seekp(m_bufferIndex, std::ios::beg);

	if (!m_buffer.empty())
	{
		file.write((const char*)&m_buffer[0], m_buffer.size());
	}
	
	file.close();

	m_fileSize = m_bufferIndex + m_buffer.size();

	m_bufferIndex = m_fileSize;
	m_buffer.clear();

	return true;
}

void File::Append(const std::vector<unsigned char>& data)
{
	m_buffer.insert(m_buffer.end(), data.cbegin(), data.cend());
}

bool File::Rewind(const uint64_t nextPosition)
{
	if (!Flush())
	{
		return false;
	}

	m_bufferIndex = nextPosition;
	//if (nextPosition <= m_bufferIndex)
	//{
	//	m_buffer.clear();
	//	m_bufferIndex = nextPosition;
	//}
	//else
	//{
	//	m_buffer.erase(m_buffer.begin() + nextPosition - m_bufferIndex, m_buffer.end());
	//}

	return true;
}

bool File::Discard()
{
	m_bufferIndex = m_fileSize;
	m_buffer.clear();

	return true;
}

uint64_t File::GetSize() const
{
	return m_bufferIndex + m_buffer.size();
}

// TODO: Change implementation to handle reading from end of file and then onto buffer
bool File::Read(const uint64_t position, const uint64_t numBytes, std::vector<unsigned char>& data) const
{
	if (position < m_bufferIndex)
	{
		std::ifstream file(m_path, std::ios::in | std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			return false;
		}

		data.resize((size_t)numBytes);

		file.seekg(position, std::ios::beg);
		file.read((char*)&data[0], numBytes);
		file.close();
	}
	else
	{
		const uint64_t firstBufferIndex = position - m_bufferIndex;

		data.insert(data.end(), m_buffer.cbegin() + firstBufferIndex, m_buffer.cbegin() + firstBufferIndex + numBytes);
	}

	return true;
}