#pragma once

namespace common
{

class ScopedLambda
{
public:

	ScopedLambda(std::function<void()> releaseFunction)
	: mReleaseFunction(releaseFunction)
	{
	}

	~ScopedLambda()
	{
		try
		{
			if (mReleaseFunction)
			{
				mReleaseFunction();
			}
		}
		catch(...)
		{
		}
	}

	ScopedLambda() = delete;

private:

	std::function<void()> mReleaseFunction;
};

} // namespace common
