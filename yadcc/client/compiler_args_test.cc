// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this
// file except in compliance with the License. You may obtain a copy of the
// License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "yadcc/client/compiler_args.h"

#include "thirdparty/googletest/gtest/gtest.h"

namespace yadcc::client {

TEST(CompilerArgs, All) {
  const char* argvs[] = {"-c", "-std=c++11", "-o", "1.o", "1.cc"};
  CompilerArgs args(std::size(argvs), argvs);

  EXPECT_TRUE(args.TryGet("-std=c++11"));
  EXPECT_FALSE(args.TryGet("-std1=c++11"));
  EXPECT_TRUE(args.TryGetByPrefix("-std="));
  EXPECT_FALSE(args.TryGetByPrefix("-std1="));
  EXPECT_EQ(1, args.GetFilenames().size());
  EXPECT_EQ("1.cc", args.GetFilenames()[0]);
  EXPECT_EQ("1.cc", args.GetFilenames()[0]);

  args.SetCompiler("g++");
  EXPECT_EQ("g++ -c -std=c++11 -o 1.o 1.cc", args.Rebuild());
  EXPECT_EQ("g++ -c -o 1.o -std=c++2a 1.cc",
            args.Rewrite({"-std=c++11"}, {}, {"-std=c++2a"}, true)
                .ToCommandLine(true));
  EXPECT_EQ("g++ -c -std=c++2a 1.cc",
            args.Rewrite({"-o"}, {"-std="}, {"-std=c++2a"}, true)
                .ToCommandLine(true));
}

TEST(CompilerArgs, Escape) {
  const char* argvs[] = {R"(-x="")", "1.cc"};
  CompilerArgs args(std::size(argvs), argvs);
  args.SetCompiler("g++");
  EXPECT_EQ(R"(g++ -x=\"\" 1.cc)",
            args.Rewrite({}, {}, {}, true).ToCommandLine(true));
}

}  // namespace yadcc::client
