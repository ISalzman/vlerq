package vlerq_test

import (
	. "github.com/jcw/vlerq"
	. "github.com/onsi/ginkgo"
	. "github.com/onsi/gomega"
)

var _ = Describe("Flow", func() {

	Describe("Create a View", func() {
		v := new(View)

		It("should not be nil", func() {
			Expect(v).ShouldNot(BeNil())
		})
	})
})
