package vlerq_test

import (
	// . "github.com/jcw/vlerq"
	. "github.com/onsi/ginkgo"
	. "github.com/onsi/gomega"
)

var _ = Describe("Flow", func() {

	Describe("Create a Gadget", func() {
		g := 1

		It("should not be nil", func() {
			Expect(g).ShouldNot(BeNil())
		})
	})
})
